#include "crafter/Payload.h"
#include "crafter/Protocols/RawLayer.h"
#include "crafter/Utils/TCPConnection.h"
#include "logging.h"
#include <chrono>
#include <crafter.h>
#include <cstddef>
#include <cstdlib>
#include <net/ethernet.h>
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <pcap/pcap.h>
#include <thread>
#include <tuple>
#include <unistd.h>

using namespace Crafter;

std::string client_ip;
std::string server_ip;
int client_port;
int server_port;
pcap_t *handle;
int linkhdrlen = 14;
int packets = 0;
int client_fd;
int fd;
int new_fd;
// assuming they are continuous
struct connection_pair {
    std::jthread *send;
    std::jthread *recv;
    int server_fd;
    int new_server;
    int new_client;
    bool is_sleep;
};
std::map<std::string, struct connection_pair> tcp_pair;
std::vector<std::tuple<std::string, std::string, std::string>> forward_pair;
std::vector<std::jthread> backend_thread;
bool is_forward = false;
int id = 0;
struct mvvm_op_data *op_data;
// Function to recalculate the IP checksum
unsigned short in_cksum(unsigned short *buf, int len) {
    unsigned long sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len)
        sum += *(unsigned char *)buf;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}
// https://github.com/pellegre/libcrafter-examples/blob/03832c5c6f68b55a714877bf53aaba2fc33c43ff/SimpleHijackConnection/main.cpp#L113
void forward(const unsigned char *buf, int len) {
    int sock;
    struct sockaddr_in dst {};

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = ((const struct iphdr *)buf)->daddr;
    sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sendto(sock, buf, len, 0, (struct sockaddr *)&dst, sizeof(dst)) != len) {
        LOGV(ERROR) << "forward: sending failed.";
        return;
    } else {
        LOGV(INFO) << "DEBUG: forward: sending succeeded.";
        return;
    }
}
void forwardv6(const unsigned char *buf, int len) {
    int sock;
    struct sockaddr_in6 dst {};

    memset(&dst, 0, sizeof(dst));
    dst.sin6_family = AF_INET6;

    // Extract the destination address from the IPv6 header
    dst.sin6_addr = ((const struct ip6_hdr *)buf)->ip6_dst;

    // Create a raw socket with IPv6 domain
    sock = socket(AF_INET6, SOCK_RAW, IPPROTO_RAW);
    if (sock < 0) {
        LOGV(ERROR) << "forwardv6: socket creation failed.";
        return;
    }

    if (sendto(sock, buf, len, 0, (struct sockaddr *)&dst, sizeof(dst)) != len) {
        LOGV(ERROR) << "forwardv6: sending failed.";
    } else {
        LOGV(INFO) << "forwardv6: sending succeeded.";
    }

    // Close the socket
    close(sock);
}

// can be rewrite by libcraft?
void packet_handler(u_char *user, const struct pcap_pkthdr *header, const u_char *packetptr) {
    // Analyze packet
    struct ip6_hdr *ipv6hdr;
    struct icmp6_hdr *icmp6hdr;
    struct ip *iphdr;
    struct icmp *icmphdr;
    struct tcphdr *tcphdr;
    struct udphdr *udphdr;
    char srcip[256];
    char dstip[256];
    packetptr += linkhdrlen;
    iphdr = (struct ip *)packetptr;
    if (iphdr->ip_v == 6) {
        LOGV(INFO) << "ipv6";
        packetptr += linkhdrlen;
        ipv6hdr = (struct ip6_hdr *)packetptr;
        inet_ntop(AF_INET6, &ipv6hdr->ip6_src, srcip, sizeof(srcip));
        inet_ntop(AF_INET6, &ipv6hdr->ip6_dst, dstip, sizeof(dstip));

        // Advance to the transport layer header then parse and display
        // the fields based on the type of header: TCP, UDP, or ICMPv6.
        packetptr += sizeof(struct ip6_hdr);
        switch (ipv6hdr->ip6_nxt) {
        case IPPROTO_TCP:
            tcphdr = (struct tcphdr *)packetptr;
            LOGV(INFO) << fmt::format("TCP6  {}:{} -> {}:{}", srcip, ntohs(tcphdr->th_sport), dstip,
                                      ntohs(tcphdr->th_dport));
            LOGV(INFO) << fmt::format("ID:{} TOS:0x{}, TTL:{} IpLen:{} DgLen:{}", ntohs(iphdr->ip_id), iphdr->ip_tos,
                                      iphdr->ip_ttl, 4 * iphdr->ip_hl, ntohs(iphdr->ip_len));
            LOGV(INFO) << fmt::format("{}{}{}{}{}{} Seq: 0x{} Ack: 0x{} Win: 0x{} TcpLen: {}",
                                      (tcphdr->th_flags & TH_URG ? 'U' : '*'), (tcphdr->th_flags & TH_ACK ? 'A' : '*'),
                                      (tcphdr->th_flags & TH_PUSH ? 'P' : '*'), (tcphdr->th_flags & TH_RST ? 'R' : '*'),
                                      (tcphdr->th_flags & TH_SYN ? 'S' : '*'), (tcphdr->th_flags & TH_SYN ? 'F' : '*'),
                                      ntohl(tcphdr->th_seq), ntohl(tcphdr->th_ack), ntohs(tcphdr->th_win),
                                      4 * tcphdr->th_off);
            LOGV(INFO) << fmt::format("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
            packets += 1;
            break;

        case IPPROTO_UDP:
            udphdr = (struct udphdr *)packetptr;
            LOGV(INFO) << fmt::format("UDP6  {}:{} -> {}:{}", srcip, ntohs(udphdr->uh_sport), dstip,
                                      ntohs(udphdr->uh_dport));
            LOGV(INFO) << fmt::format("ID:{} TOS:0x{}, TTL:{} IpLen:{} DgLen:{}", ntohs(iphdr->ip_id), iphdr->ip_tos,
                                      iphdr->ip_ttl, 4 * iphdr->ip_hl, ntohs(iphdr->ip_len));
            LOGV(INFO) << fmt::format("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
            packets += 1;
            break;

        case IPPROTO_ICMPV6:
            icmp6hdr = (struct icmp6_hdr *)packetptr;
            LOGV(INFO) << fmt::format("ICMP6 {} -> {}", srcip, dstip);
            LOGV(INFO) << fmt::format("ID:{} TOS:0x{}, TTL:{} IpLen:{} DgLen:{}", ntohs(iphdr->ip_id), iphdr->ip_tos,
                                      iphdr->ip_ttl, 4 * iphdr->ip_hl, ntohs(iphdr->ip_len));
            LOGV(INFO) << fmt::format("Type:{} Code:{}", icmp6hdr->icmp6_type, icmp6hdr->icmp6_code);
            LOGV(INFO) << fmt::format("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
            packets += 1;
            break;
        }
        if (is_forward && !op_data->is_tcp) { // Skip the datalink layer header and get the IP header fields.
            // init the socket
            for (auto [srcip, destip, new_srcip] : forward_pair) {
                if (srcip == inet_ntoa(iphdr->ip_src) && destip == inet_ntoa(iphdr->ip_dst)) {
                    iphdr->ip_src.s_addr = inet_addr(new_srcip.c_str());
                    // Recalculate the IP checksum
                    iphdr->ip_sum = 0;
                    iphdr->ip_sum = in_cksum((unsigned short *)iphdr, iphdr->ip_hl * 4);

                    forward(reinterpret_cast<const unsigned char *>(iphdr), header->len);
                    return;
                }
                if (srcip == inet_ntoa(iphdr->ip_dst) && destip == inet_ntoa(iphdr->ip_src)) {
                    iphdr->ip_dst.s_addr = inet_addr(new_srcip.c_str());
                    // Recalculate the IP checksum
                    iphdr->ip_sum = 0;
                    iphdr->ip_sum = in_cksum((unsigned short *)iphdr, iphdr->ip_hl * 4);

                    forward(reinterpret_cast<const unsigned char *>(iphdr), header->len);
                    return;
                }
            }
        }

    } else {
        strcpy(srcip, inet_ntoa(iphdr->ip_src));
        strcpy(dstip, inet_ntoa(iphdr->ip_dst));

        // Advance to the transport layer header then parse and display
        // the fields based on the type of hearder: tcp, udp or icmp.
        packetptr += 4 * iphdr->ip_hl;
        switch (iphdr->ip_p) {
        case IPPROTO_TCP:
            tcphdr = (struct tcphdr *)packetptr;
            LOGV(INFO) << fmt::format("TCP  {}:{} -> {}:{}", srcip, ntohs(tcphdr->th_sport), dstip,
                                      ntohs(tcphdr->th_dport));
            LOGV(INFO) << fmt::format("ID:{} TOS:0x{}, TTL:{} IpLen:{} DgLen:{}", ntohs(iphdr->ip_id), iphdr->ip_tos,
                                      iphdr->ip_ttl, 4 * iphdr->ip_hl, ntohs(iphdr->ip_len));
            LOGV(INFO) << fmt::format("{}{}{}{}{}{} Seq: 0x{} Ack: 0x{} Win: 0x{} TcpLen: {}",
                                      (tcphdr->th_flags & TH_URG ? 'U' : '*'), (tcphdr->th_flags & TH_ACK ? 'A' : '*'),
                                      (tcphdr->th_flags & TH_PUSH ? 'P' : '*'), (tcphdr->th_flags & TH_RST ? 'R' : '*'),
                                      (tcphdr->th_flags & TH_SYN ? 'S' : '*'), (tcphdr->th_flags & TH_SYN ? 'F' : '*'),
                                      ntohl(tcphdr->th_seq), ntohl(tcphdr->th_ack), ntohs(tcphdr->th_win),
                                      4 * tcphdr->th_off);
            LOGV(INFO) << fmt::format("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
            packets += 1;
            break;

        case IPPROTO_UDP:
            udphdr = (struct udphdr *)packetptr;
            LOGV(INFO) << fmt::format("UDP  {}:{} -> {}:{}", srcip, ntohs(udphdr->uh_sport), dstip,
                                      ntohs(udphdr->uh_dport));
            LOGV(INFO) << fmt::format("ID:{} TOS:0x{}, TTL:{} IpLen:{} DgLen:{}", ntohs(iphdr->ip_id), iphdr->ip_tos,
                                      iphdr->ip_ttl, 4 * iphdr->ip_hl, ntohs(iphdr->ip_len));
            id = ntohs(iphdr->ip_id) + 1;
            LOGV(ERROR) << "id" << id;
            LOGV(INFO) << fmt::format("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");

            for (int idx = 0; idx < op_data->size; idx++)
                if (op_data->addr[idx][0].port == 0) {
                    op_data->addr[idx][0].port = udphdr->uh_sport;
                }
            packets += 1;

            break;

        case IPPROTO_ICMP:
            icmphdr = (struct icmp *)packetptr;
            LOGV(INFO) << fmt::format("ICMP {} -> {}", srcip, dstip);
            LOGV(INFO) << fmt::format("ID:{} TOS:0x{}, TTL:{} IpLen:{} DgLen:{}", ntohs(iphdr->ip_id), iphdr->ip_tos,
                                      iphdr->ip_ttl, 4 * iphdr->ip_hl, ntohs(iphdr->ip_len));
            LOGV(INFO) << fmt::format("Type:{} Code:{} ID:{} Seq:{}", icmphdr->icmp_type, icmphdr->icmp_code,
                                      ntohs(icmphdr->icmp_hun.ih_idseq.icd_id),
                                      ntohs(icmphdr->icmp_hun.ih_idseq.icd_seq));
            LOGV(INFO) << fmt::format("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
            packets += 1;
            break;
        }
        if (is_forward && !op_data->is_tcp) { // Skip the datalink layer header and get the IP header fields.
            // init the socket
            for (auto [srcip_, destip_, new_srcip] : forward_pair) {
                LOGV(DEBUG) << header->len << "srcip:" << srcip_ << " destip:" << destip_;

                if (srcip_ == inet_ntoa(iphdr->ip_src) && destip_ == inet_ntoa(iphdr->ip_dst)) {
                    iphdr->ip_src.s_addr = inet_addr(new_srcip.c_str());
                    // Recalculate the IP checksum
                    iphdr->ip_sum = 0;
                    iphdr->ip_sum = in_cksum((unsigned short *)iphdr, iphdr->ip_hl * 4);

                    forward(reinterpret_cast<const unsigned char *>(iphdr), header->len);
                    return;
                }
                if (srcip_ == inet_ntoa(iphdr->ip_dst) && destip_ == inet_ntoa(iphdr->ip_src)) {
                    iphdr->ip_dst.s_addr = inet_addr(new_srcip.c_str());
                    // Recalculate the IP checksum
                    iphdr->ip_sum = 0;
                    iphdr->ip_sum = in_cksum((unsigned short *)iphdr, iphdr->ip_hl * 4);

                    forward(reinterpret_cast<const unsigned char *>(iphdr), header->len);
                    return;
                }
            }
        }
    }
}
void pcap_loop_wrapper(const std::stop_token &stopToken, pcap_t *handle, pcap_handler packet_handler) {
    while (!stopToken.stop_requested())
        pcap_loop(handle, -1, packet_handler, nullptr);
}

void send_fin(std::string source_ip, int source_port, std::string dest_ip, int dest_port, const char *payload) {
    // Send keep alive message to socket
    // Initialize Libcrafter
    InitCrafter();
    // Create an IP layer
    IP ip_layer;
    ip_layer.SetSourceIP(source_ip);
    ip_layer.SetDestinationIP(dest_ip);
    ip_layer.SetIdentification(21180);
    // Create a UDP layer
    UDP udp_layer;
    udp_layer.SetSrcPort(source_port);
    udp_layer.SetDstPort(dest_port);
    udp_layer.SetPayload(payload);

    // Craft the packet
    Packet packet = ip_layer / udp_layer;
    ip_layer.SetCheckSum(in_cksum((unsigned short *)packet.GetRawPtr(), packet.GetSize()));
    // Send the packet
    packet.Send(MVVM_SOCK_INTERFACE);
    // Clean up Libcrafter
    CleanCrafter();
}

void sigterm_handler(int sig) {
    struct pcap_stat stats {};

    if (pcap_stats(handle, &stats) >= 0) {
        LOGV(INFO) << fmt::format("{} packets captured", packets);
        LOGV(INFO) << fmt::format("{} packets received by filter", stats.ps_recv);
        LOGV(INFO) << fmt::format("{} packets dropped", stats.ps_drop);
    }
    pcap_close(handle);
    close(client_fd);
    close(fd);
    LOGV(INFO) << "Bye";
    exit(0);
}
// int main(){
//     struct mvvm_op_data *op_data = (struct mvvm_op_data *)malloc(sizeof(struct mvvm_op_data));
//     op_data->op = MVVM_SOCK_FIN;
//     send_fin("172.17.0.3",12346,"172.17.0.2",15772,((char *)op_data));
// }
int main() {
    struct sockaddr_in address {};
    int opt = 1;
    ssize_t rc;
    int addrlen = sizeof(address);
    char buffer[1024], buffer1[1024] = {0};
    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program fp {};
    // char filter_exp[] = ""; // The filter expression
    char filter_exp[] = "net 172.17.0.0/24";
    bpf_u_int32 netmask;

    op_data = (struct mvvm_op_data *)malloc(sizeof(struct mvvm_op_data));

    signal(SIGTERM, sigterm_handler);
    signal(SIGQUIT, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    fd = socket(AF_INET, SOCK_STREAM, 0); // Create a socket

    // Forcefully attaching socket to the port
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        LOGV(ERROR) << "setsockopt";
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(MVVM_SOCK_PORT);
    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, MVVM_SOCK_ADDR, &address.sin_addr) <= 0) {
        LOGV(ERROR) << "Invalid address/ Address not supported";
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the network address and port
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOGV(ERROR) << "bind failed";
        exit(EXIT_FAILURE);
    }

    // Start listening for connections
    if (listen(fd, 3) < 0) {
        LOGV(ERROR) << "listen";
        exit(EXIT_FAILURE);
    }

    handle = pcap_open_live(MVVM_SOCK_INTERFACE, BUFSIZ, 1, 1000, errbuf);
    if (handle == nullptr) {
        LOGV(ERROR) << fmt::format("pcap_open_live(): {}", errbuf);
        exit(EXIT_FAILURE);
    }

    // Compile and apply the filter
    if (pcap_compile(handle, &fp, filter_exp, 1, PCAP_NETMASK_UNKNOWN) == -1) {
        LOGV(ERROR) << fmt::format("Couldn't parse filter {}: {}", filter_exp, pcap_geterr(handle));
        exit(-1);
    }

    if (pcap_setfilter(handle, &fp) == -1) {
        LOGV(ERROR) << fmt::format("Couldn't install filter {}:", filter_exp, pcap_geterr(handle));
        exit(-1);
    }

    // Capture packets
    backend_thread.emplace_back(pcap_loop_wrapper, handle, packet_handler);

    while (true) { // Open the device for sniffing
        client_fd = accept(fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_fd < 0) {
            LOGV(ERROR) << "accept";
            exit(EXIT_FAILURE);
        }
        // offload info from client
        if ((rc = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
            memcpy(op_data, buffer, sizeof(*op_data));
            switch (op_data->op) {
            case MVVM_SOCK_SUSPEND: {
                // suspend
                LOGV(ERROR) << "suspend";

                for (int idx = 0; idx < op_data->size; idx++) {
                    if (op_data->addr[idx][0].is_4) {
                        server_ip =
                            fmt::format("{}.{}.{}.{}", op_data->addr[idx][0].ip4[0], op_data->addr[idx][0].ip4[1],
                                        op_data->addr[idx][0].ip4[2], op_data->addr[idx][0].ip4[3]);
                        client_ip =
                            fmt::format("{}.{}.{}.{}", op_data->addr[idx][1].ip4[0], op_data->addr[idx][1].ip4[1],
                                        op_data->addr[idx][1].ip4[2], op_data->addr[idx][1].ip4[3]);
                    } else {
                        server_ip = fmt::format("{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}",
                                                op_data->addr[idx][0].ip6[0], op_data->addr[idx][0].ip6[1],
                                                op_data->addr[idx][0].ip6[2], op_data->addr[idx][0].ip6[3],
                                                op_data->addr[idx][0].ip6[4], op_data->addr[idx][0].ip6[5],
                                                op_data->addr[idx][0].ip6[6], op_data->addr[idx][0].ip6[7]);
                        client_ip = fmt::format("{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}",
                                                op_data->addr[idx][1].ip6[0], op_data->addr[idx][1].ip6[1],
                                                op_data->addr[idx][1].ip6[2], op_data->addr[idx][1].ip6[3],
                                                op_data->addr[idx][1].ip6[4], op_data->addr[idx][1].ip6[5],
                                                op_data->addr[idx][1].ip6[6], op_data->addr[idx][1].ip6[7]);
                    }
                    server_port = op_data->addr[0][0].port;
                    client_port = op_data->addr[0][1].port;
                    LOGV(INFO) << "server_ip:" << server_ip << ":" << server_port << " client_ip:" << client_ip << ":"
                               << client_port;

                    forward_pair.emplace_back(server_ip, client_ip, "");
                    // send the fin to server
                    op_data->op = MVVM_SOCK_FIN;
                    LOGV(INFO) << "send fin";

                    if (!op_data->is_tcp) {
                        send_fin(client_ip, client_port, server_ip, server_port, (char *)op_data);
                    } else {

                        auto to_stop = tcp_pair[fmt::format("{}:{}", server_ip, server_port)];
                        LOGV(ERROR) << server_ip << ":" << server_port << " " << to_stop.new_client << " "
                                    << to_stop.new_server;
                        sleep(2);
                        send(to_stop.new_server, (char *)op_data, sizeof(*op_data), 0);
                        to_stop.is_sleep = true;
                        delete to_stop.send;
                        delete to_stop.recv;
                    }
                }
                break;
            }
            case MVVM_SOCK_RESUME: {
                // resume
                LOGV(ERROR) << "resume";
                auto tmp_tuple = forward_pair[forward_pair.size() - 1];
                auto &&tmp_ip4 = op_data->addr[0][0].ip4;
                std::get<2>(tmp_tuple) = fmt::format("{}.{}.{}.{}", tmp_ip4[0], tmp_ip4[1], tmp_ip4[2], tmp_ip4[3]);
                LOGV(ERROR) << "forward_pair[forward_pair.size()]" << std::get<0>(forward_pair[forward_pair.size() - 1])
                            << std::get<1>(forward_pair[forward_pair.size() - 1]);

                if (op_data->is_tcp) {
                    auto to_start = tcp_pair[fmt::format("{}:{}", server_ip, server_port)];
                    LOGV(ERROR) << server_ip << ":" << server_port;

                    if (to_start.new_server == 0) {
                        LOGV(ERROR) << "to_start is empty";
                        exit(-1);
                    } else {
                        to_start.is_sleep = false;
                    }

                    socklen_t size = sizeof(address);
                    auto new_server = accept(to_start.server_fd, (struct sockaddr *)&address, &size);
                    int new_client = to_start.new_client;

                    to_start.send = new std::jthread([&](std::stop_token stopToken) {
                        while (!stopToken.stop_requested()) {
                            if ((rc = recv(new_server, buffer1, sizeof(buffer1), 0)) > 0) {
                                LOGV(ERROR) << "send" << sizeof(buffer1);
                                send(new_client, buffer1, sizeof(buffer1), 0);
                            }
                        }
                    });
                    to_start.recv = new std::jthread([&](std::stop_token stopToken) {
                        // Set the socket to non-blocking
                        fcntl(new_client, F_SETFL, O_NONBLOCK);

                        while (!stopToken.stop_requested()) {
                            rc = recv(new_client, buffer, sizeof(buffer), 0);
                            if (rc > 0) {
                                LOGV(ERROR) << "recv" << sizeof(buffer);
                                while ((rc = send(new_server, buffer, sizeof(buffer), 0) <= 0)) {
                                    LOGV(ERROR) << "error sending" << rc << errno;
                                    sleep(1);
                                }
                            } else if (rc == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                                // Handle error
                                break;
                            }
                            // Add a small sleep to prevent busy-waiting
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    });
                    to_start.new_server = new_server;
                    tcp_pair[fmt::format("{}:{}", server_ip, server_port)] = to_start;
                } else
                    is_forward = true;
                break;
            }
            case MVVM_SOCK_INIT: {
                // init
                LOGV(ERROR) << "init";
                if (op_data->addr[0][0].is_4) {
                    server_ip = fmt::format("{}.{}.{}.{}", op_data->addr[0][0].ip4[0], op_data->addr[0][0].ip4[1],
                                            op_data->addr[0][0].ip4[2], op_data->addr[0][0].ip4[3]);
                    client_ip = fmt::format("{}.{}.{}.{}", op_data->addr[0][1].ip4[0], op_data->addr[0][1].ip4[1],
                                            op_data->addr[0][1].ip4[2], op_data->addr[0][1].ip4[3]);
                } else {
                    server_ip =
                        fmt::format("{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}",
                                    op_data->addr[0][0].ip6[0], op_data->addr[0][0].ip6[1], op_data->addr[0][0].ip6[2],
                                    op_data->addr[0][0].ip6[3], op_data->addr[0][0].ip6[4], op_data->addr[0][0].ip6[5],
                                    op_data->addr[0][0].ip6[6], op_data->addr[0][0].ip6[7]);
                    client_ip =
                        fmt::format("{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}",
                                    op_data->addr[0][1].ip6[0], op_data->addr[0][1].ip6[1], op_data->addr[0][1].ip6[2],
                                    op_data->addr[0][1].ip6[3], op_data->addr[0][1].ip6[4], op_data->addr[0][1].ip6[5],
                                    op_data->addr[0][1].ip6[6], op_data->addr[0][1].ip6[7]);
                }
                server_port = op_data->addr[0][0].port;
                client_port = op_data->addr[0][1].port;

                LOGV(INFO) << "server_ip:" << server_ip << ":" << server_port << " client_ip:" << client_ip << ":"
                           << client_port;

                int server_fd = socket(AF_INET, SOCK_STREAM, 0); // Create a socket

                // Forcefully attaching socket to the port
                if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
                    LOGV(ERROR) << "setsockopt";
                    exit(EXIT_FAILURE);
                }

                address.sin_family = AF_INET;
                address.sin_port = htons(client_port);
                // Convert IPv4 and IPv6 addresses from text to binary form
                if (inet_pton(AF_INET, MVVM_SOCK_ADDR, &address.sin_addr) <= 0) {
                    LOGV(ERROR) << "Invalid address/ Address not supported";
                    exit(EXIT_FAILURE);
                }

                // Bind the socket to the network address and port
                if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
                    LOGV(ERROR) << "bind failed" << errno;
                    exit(EXIT_FAILURE);
                }

                // Start listening for connections
                if (listen(server_fd, 3) < 0) {
                    LOGV(ERROR) << "listen";
                    exit(EXIT_FAILURE);
                }
                // sleep(1);
                int new_server =
                    accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen); // will be instantly consumed
                // Create a socket connect remote
                int new_client = socket(AF_INET, SOCK_STREAM, 0);
                // Convert IPv4 and IPv6 addresses from text to binary form

                address.sin_family = AF_INET;
                address.sin_port = htons(client_port);
                if (inet_pton(AF_INET, client_ip.c_str(), &address.sin_addr) <= 0) {
                    LOGV(ERROR) << "Invalid address/ Address not supported";
                    exit(EXIT_FAILURE);
                }

                if (connect(new_client, (struct sockaddr *)&address, sizeof(address)) == -1) {
                    LOGV(ERROR) << "connect failed " << errno;
                    close(new_client);
                    exit(EXIT_FAILURE);
                }
                LOGV(ERROR) << "new_client " << new_client;
                struct connection_pair cp = {
                    .server_fd = server_fd, .new_server = new_server, .new_client = new_client, .is_sleep = false};
                cp.send = new std::jthread([&](std::stop_token stopToken) {
                    while (!stopToken.stop_requested()) {
                        if ((rc = recv(new_server, buffer1, sizeof(buffer1), 0)) > 0) {
                            LOGV(ERROR) << "send" << sizeof(buffer1);
                            while ((rc = send(new_client, buffer1, sizeof(buffer1), 0) <= 0)) {
                                LOGV(ERROR) << "error sending" << rc << errno;
                                sleep(1);
                            }
                        }
                    }
                });
                cp.recv = new std::jthread([&](std::stop_token stopToken) {
                    // Set the socket to non-blocking
                    fcntl(new_client, F_SETFL, O_NONBLOCK);

                    while (!stopToken.stop_requested()) {
                        rc = recv(new_client, buffer, sizeof(buffer), 0);
                        if (rc > 0) {
                            LOGV(ERROR) << "recv" << sizeof(buffer);
                            while ((rc = send(new_server, buffer, sizeof(buffer), 0) <= 0)) {
                                LOGV(ERROR) << "error sending" << rc << errno;
                                sleep(1);
                            }
                        } else if (rc == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                            // Handle error
                            break;
                        }
                        // Add a small sleep to prevent busy-waiting
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                });
                // client send this for the server so reverse order
                LOGV(ERROR) << client_ip << ":" << client_port;
                tcp_pair[fmt::format("{}:{}", client_ip, client_port)] = cp;
                break;
            }
            }
        }
    }
}
