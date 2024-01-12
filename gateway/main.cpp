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

using namespace Crafter;

std::string client_ip;
std::string server_ip;
pcap_t *handle;
int linkhdrlen = 14;
int packets = 0;
int client_fd;
int fd;
std::vector<std::jthread> backend_thread;
std::vector<std::tuple<std::string, std::string, std::string>> forward_pair;
bool is_forward = false;

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
        if (is_forward) { // Skip the datalink layer header and get the IP header fields.
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
            LOGV(INFO) << fmt::format("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+");
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
        if (is_forward) { // Skip the datalink layer header and get the IP header fields.
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

void keep_alive(const std::stop_token &stopToken, std::string source_ip, int source_port, std::string dest_ip,
                int dest_port) {
    // Send keep alive message to socket
    // Initialize Libcrafter
    InitCrafter();

    while (!stopToken.stop_requested()) {
        // Create an IP layer
        IP ip_layer;
        ip_layer.SetSourceIP(source_ip);
        ip_layer.SetDestinationIP(dest_ip);

        // Create a TCP layer
        TCP tcp_layer;
        tcp_layer.SetSrcPort(source_port);
        tcp_layer.SetDstPort(dest_port);
        tcp_layer.SetFlags(TCP::ACK);

        // Craft the packet
        Packet packet = ip_layer / tcp_layer;

        // Send the packet
        packet.Send();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
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

int main() {
    struct sockaddr_in address {};
    int opt = 1;
    ssize_t rc;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program fp {};
    // char filter_exp[] = ""; // The filter expression
    char filter_exp[] = "net 172.17.0.0/24";
    bpf_u_int32 netmask;

    struct mvvm_op_data op_data {};

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
            memcpy(&op_data, buffer, sizeof(op_data));
            switch (op_data.op) {
            case MVVM_SOCK_SUSPEND:
                // suspend
                LOGV(ERROR) << "suspend"; // capture all the packets from dest to source

                for (int idx = 0; idx < op_data.size; idx++) {
                    if (op_data.addr[idx][0].is_4) {
                        server_ip = fmt::format("{}.{}.{}.{}", op_data.addr[idx][0].ip4[0], op_data.addr[idx][0].ip4[1],
                                                op_data.addr[idx][0].ip4[2], op_data.addr[idx][0].ip4[3]);
                        client_ip = fmt::format("{}.{}.{}.{}", op_data.addr[idx][1].ip4[0], op_data.addr[idx][1].ip4[1],
                                                op_data.addr[idx][1].ip4[2], op_data.addr[idx][1].ip4[3]);
                    } else {
                        server_ip = fmt::format("{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}",
                                                op_data.addr[idx][0].ip6[0], op_data.addr[idx][0].ip6[1],
                                                op_data.addr[idx][0].ip6[2], op_data.addr[idx][0].ip6[3],
                                                op_data.addr[idx][0].ip6[4], op_data.addr[idx][0].ip6[5],
                                                op_data.addr[idx][0].ip6[6], op_data.addr[idx][0].ip6[7]);
                        client_ip = fmt::format("{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}",
                                                op_data.addr[idx][1].ip6[0], op_data.addr[idx][1].ip6[1],
                                                op_data.addr[idx][1].ip6[2], op_data.addr[idx][1].ip6[3],
                                                op_data.addr[idx][1].ip6[4], op_data.addr[idx][1].ip6[5],
                                                op_data.addr[idx][1].ip6[6], op_data.addr[idx][1].ip6[7]);
                    }
                    LOGV(INFO) << "server_ip:" << server_ip << " client_ip:" << client_ip;
                    if (op_data.is_tcp)
                        backend_thread.emplace_back(keep_alive, server_ip, op_data.addr[idx][0].port, client_ip,
                                                    op_data.addr[idx][1].port); // server to client? client to server?
                    forward_pair.emplace_back(server_ip, client_ip, "");
                    // send the fin to server
                }
                // send fin
                op_data.op = MVVM_SOCK_FIN;
                if (op_data.is_tcp) {
                    // send (client_fd, &op_data, sizeof(op_data), 0);
                    sendto(client_fd, &op_data, sizeof(op_data), 0, (struct sockaddr *)&address, sizeof(address));
                } else {
                    send(client_fd, &op_data, sizeof(op_data), 0);
                }
                break;
            case MVVM_SOCK_RESUME:
                // resume
                LOGV(ERROR) << "resume";
                auto tmp_tuple = forward_pair[forward_pair.size() - 1];
                std::get<2>(tmp_tuple) =
                    fmt::format("{}.{}.{}.{}", op_data.addr[0][0].ip4[0], op_data.addr[0][0].ip4[1],
                                op_data.addr[0][0].ip4[2], op_data.addr[0][0].ip4[3]);
                LOGV(ERROR) << "forward_pair[forward_pair.size()]" << std::get<0>(forward_pair[forward_pair.size() - 1])
                            << std::get<1>(forward_pair[forward_pair.size() - 1]);
                is_forward = true;
                // for udp forward from source to remote
                // stop keep_alive
                if (op_data.is_tcp) {
                    backend_thread.pop_back();
                }
                sleep(1);
                break;
            case MVVM_SOCK_FIN:
                break;
            }
        }
    }
}