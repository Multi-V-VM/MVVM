// TCP capture/replay gateway. Capture is passive; replay creates fresh kernel sockets.
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <netinet/tcp.h>
#include <pcap/pcap.h>
#include <poll.h>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
using Bytes = std::vector<unsigned char>;
constexpr size_t limit = 64 * 1024 * 1024;
void require(bool b, const std::string &s) {
    if (!b)
        throw std::runtime_error(s);
}
unsigned u16(const unsigned char *p) { return unsigned(p[0]) * 256 + p[1]; }
uint32_t u32(const unsigned char *p) { return uint32_t(u16(p)) << 16 | u16(p + 2); }
struct Endpoint {
    uint32_t ip;
    unsigned port;
    bool operator==(const Endpoint &) const = default;
};
Endpoint endpoint(const char *ip, const char *port) {
    in_addr a{};
    require(inet_pton(AF_INET, ip, &a) == 1, "IPv4 address required");
    size_t end{};
    auto n = std::stoul(port, &end);
    require(end == strlen(port) && n > 0 && n <= 65535, "invalid port");
    return {ntohl(a.s_addr), unsigned(n)};
}
sockaddr_in address(Endpoint e) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(e.port);
    a.sin_addr.s_addr = htonl(e.ip);
    return a;
}
struct Pcap {
    pcap_t *p{};
    ~Pcap() {
        if (p)
            pcap_close(p);
    }
};
struct FD {
    int n{-1};
    ~FD() {
        if (n >= 0)
            close(n);
    }
};
struct Packet {
    Endpoint src, dst;
    uint32_t seq, ack;
    unsigned flags;
    Bytes data;
};
Packet decode(std::span<const unsigned char> b, int link) {
    size_t pos{};
    unsigned type{};
    if (link == DLT_EN10MB) {
        require(b.size() >= 14, "short Ethernet");
        type = u16(b.data() + 12);
        pos = 14;
    } else if (link == DLT_LINUX_SLL) {
        require(b.size() >= 16, "short SLL");
        type = u16(b.data() + 14);
        pos = 16;
    } else if (link == DLT_LINUX_SLL2) {
        require(b.size() >= 20, "short SLL2");
        type = u16(b.data());
        pos = 20;
    } else
        throw std::runtime_error("unsupported link type");
    while (type == 0x8100 || type == 0x88a8) {
        require(b.size() >= pos + 4, "short VLAN");
        type = u16(b.data() + pos + 2);
        pos += 4;
    }
    require(type == 0x0800 && b.size() >= pos + 20, "expected IPv4");
    auto p = b.data() + pos;
    size_t ih = (p[0] & 15) * 4, total = u16(p + 2);
    require((p[0] >> 4) == 4 && ih >= 20 && total >= ih + 20 && b.size() >= pos + total, "truncated IPv4/TCP");
    require(!(u16(p + 6) & 0x3fff) && p[9] == 6, "fragmented or non-TCP packet");
    auto t = p + ih;
    size_t th = (t[12] >> 4) * 4;
    require(th >= 20 && ih + th <= total, "bad TCP header");
    return {{u32(p + 12), u16(t)}, {u32(p + 16), u16(t + 2)}, u32(t + 4), u32(t + 8), t[13], Bytes(t + th, p + total)};
}
struct Event {
    unsigned side;
    Bytes data;
    bool fin;
};
struct Stream {
    uint32_t isn{};
    bool syn{}, closed{};
    Bytes bytes;
    std::map<uint32_t, Bytes> pending;
    size_t pending_bytes{};
    bool have_fin{};
    uint32_t fin_at{};
};
struct Trace {
    std::vector<Event> events;
    Stream streams[2];
    size_t packets{}, duplicates{};
};
Trace load(const char *file, Endpoint server) {
    char err[PCAP_ERRBUF_SIZE]{};
    Pcap h{pcap_open_offline(file, err)};
    require(h.p, err);
    Trace tr;
    Endpoint client{};
    bool started = false, handshake = false;
    pcap_pkthdr *hdr{};
    const unsigned char *data{};
    int rc;
    while ((rc = pcap_next_ex(h.p, &hdr, &data)) >= 0) {
        if (!rc)
            continue;
        require(hdr->caplen == hdr->len, "capture truncation");
        auto p = decode({data, hdr->caplen}, pcap_datalink(h.p));
        unsigned side = p.dst == server ? 0 : 1;
        require(p.src == server || p.dst == server, "unrelated capture endpoint");
        if (!started) {
            require(side == 0 && (p.flags & TH_SYN) && !(p.flags & TH_ACK), "capture must start at client SYN");
            client = p.src;
            started = true;
        }
        require((side == 0 ? p.src : p.dst) == client, "multiple connections: capture exactly one TCP flow");
        require(!(p.flags & TH_RST), "reset connection cannot be replayed as graceful session");
        auto &s = tr.streams[side];
        auto &peer = tr.streams[1 - side];
        ++tr.packets;
        require(tr.packets <= 1000000, "packet count limit");
        if (p.flags & TH_SYN) {
            require(p.data.empty() && !(p.flags & TH_FIN), "TCP Fast Open/SYN data unsupported");
            if (s.syn)
                require(s.isn == p.seq, "new connection in capture");
            else {
                s.syn = true;
                s.isn = p.seq;
            }
            if (side == 1)
                require((p.flags & TH_ACK) && peer.syn && p.ack == peer.isn + 1, "invalid SYN ACK");
            continue;
        }
        require(s.syn && peer.syn, "missing SYN/SYN-ACK");
        if (!handshake) {
            require(side == 0 && (p.flags & TH_ACK) && p.ack == peer.isn + 1, "missing final handshake ACK");
            handshake = true;
        }
        require(p.flags & TH_ACK, "TCP segment missing ACK");
        uint32_t offset = p.seq - s.isn - 1;
        require(offset <= limit && p.data.size() <= limit - offset, "stream too large or bad sequence");
        if (!p.data.empty()) {
            size_t skip = std::min<size_t>(p.data.size(), offset < s.bytes.size() ? s.bytes.size() - offset : 0);
            require(std::equal(p.data.begin(), p.data.begin() + skip,
                               s.bytes.begin() + std::min<size_t>(offset, s.bytes.size())),
                    "conflicting retransmission");
            if (skip == p.data.size())
                ++tr.duplicates;
            else {
                require(!s.closed, "data after FIN");
                Bytes tail(p.data.begin() + skip, p.data.end());
                uint32_t at = offset + skip;
                auto [it, inserted] = s.pending.emplace(at, tail);
                if (!inserted)
                    require(it->second == tail, "conflicting queued segment");
                else
                    s.pending_bytes += tail.size();
                require(s.pending_bytes <= limit, "reassembly buffer limit");
            }
        }
        if (p.flags & TH_FIN) {
            uint32_t at = offset + p.data.size();
            require(!s.have_fin || s.fin_at == at, "conflicting FIN");
            s.have_fin = true;
            s.fin_at = at;
        }
        while (!s.pending.empty() && s.pending.begin()->first <= s.bytes.size()) {
            auto it = s.pending.begin();
            auto at = it->first;
            auto &v = it->second;
            size_t skip = std::min(v.size(), s.bytes.size() - at);
            require(std::equal(v.begin(), v.begin() + skip, s.bytes.begin() + at), "conflicting out-of-order overlap");
            if (skip < v.size()) {
                Bytes tail(v.begin() + skip, v.end());
                s.bytes.insert(s.bytes.end(), tail.begin(), tail.end());
                tr.events.push_back({side, std::move(tail), false});
            }
            s.pending_bytes -= v.size();
            s.pending.erase(it);
        }
        if (s.have_fin && !s.closed && s.bytes.size() == s.fin_at) {
            require(s.pending.empty(), "data beyond FIN");
            tr.events.push_back({side, {}, true});
            s.closed = true;
        }
        require(!s.have_fin || s.bytes.size() <= s.fin_at, "data beyond FIN sequence");
    }
    require(rc == PCAP_ERROR_BREAK, "pcap read failed");
    require(handshake, "incomplete handshake");
    for (auto &s : tr.streams)
        require(s.closed && s.pending.empty(), "incomplete capture: gap or missing FIN");
    return tr;
}
void waitfd(int fd, short events) {
    pollfd p{fd, events, 0};
    int r;
    do {
        r = poll(&p, 1, 5000);
    } while (r < 0 && errno == EINTR);
    require(r > 0, "socket timeout");
    require(!(p.revents & POLLNVAL), "invalid socket");
}
void replay(const Trace &tr, Endpoint target, bool server_role) {
    FD fd{socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)};
    require(fd.n >= 0, "socket failed");
    auto a = address(target);
    if (server_role) {
        int one = 1;
        setsockopt(fd.n, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        require(bind(fd.n, (sockaddr *)&a, sizeof(a)) == 0, "bind failed");
        require(listen(fd.n, 1) == 0, "listen failed");
        std::cout << "LISTENING\n" << std::flush;
        waitfd(fd.n, POLLIN);
        int n = accept4(fd.n, nullptr, nullptr, SOCK_NONBLOCK);
        require(n >= 0, "accept failed");
        close(fd.n);
        fd.n = n;
    } else {
        int r = connect(fd.n, (sockaddr *)&a, sizeof(a));
        require(r == 0 || errno == EINPROGRESS, "connect failed");
        if (r) {
            waitfd(fd.n, POLLOUT);
            int e{};
            socklen_t sz = sizeof(e);
            require(getsockopt(fd.n, SOL_SOCKET, SO_ERROR, &e, &sz) == 0 && e == 0, "connect completion failed");
        }
    }
    tcp_info info{};
    socklen_t sz = sizeof(info);
    require(getsockopt(fd.n, IPPROTO_TCP, TCP_INFO, &info, &sz) == 0 && (info.tcpi_state == TCP_ESTABLISHED || info.tcpi_state == TCP_CLOSE_WAIT),
            "kernel TCP not established");
    size_t sent{}, received{};
    unsigned local = server_role ? 1 : 0;
    for (auto &e : tr.events) {
        if (e.fin) {
            if (e.side == local)
                require(shutdown(fd.n, SHUT_WR) == 0, "shutdown failed");
            else {
                waitfd(fd.n, POLLIN);
                unsigned char b;
                require(recv(fd.n, &b, 1, 0) == 0, "expected EOF; extra bytes or socket error");
            }
            continue;
        }
        size_t at = 0;
        while (at < e.data.size()) {
            waitfd(fd.n, e.side == local ? POLLOUT : POLLIN);
            ssize_t n;
            if (e.side == local)
                n = send(fd.n, e.data.data() + at, e.data.size() - at, MSG_NOSIGNAL);
            else {
                std::array<unsigned char, 65536> b;
                n = recv(fd.n, b.data(), std::min(b.size(), e.data.size() - at), 0);
                if (n > 0)
                    require(std::equal(b.begin(), b.begin() + n, e.data.begin() + at),
                            std::format("replay mismatch at received byte {}", received));
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            require(n > 0, "socket closed/error during payload");
            at += n;
            if (e.side == local)
                sent += n;
            else
                received += n;
        }
    }
    std::cout << std::format(
        "{{\"kernel_established\":true,\"sent\":{},\"received\":{},\"packets\":{},\"duplicates\":{}}}\n", sent,
        received, tr.packets, tr.duplicates);
}
void record(const char *iface, Endpoint server, const char *file, unsigned seconds) {
    require(seconds > 0 && seconds <= 3600, "capture seconds must be 1..3600");
    char err[PCAP_ERRBUF_SIZE]{};
    Pcap h{pcap_create(iface, err)};
    require(h.p, err);
    require(pcap_set_snaplen(h.p, 262144) == 0 && pcap_set_immediate_mode(h.p, 1) == 0 &&
                pcap_set_buffer_size(h.p, 16 * 1024 * 1024) == 0,
            "pcap configuration failed");
    require(pcap_activate(h.p) == 0, pcap_geterr(h.p));
    auto a = address(server);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    auto filter = std::format("ip and tcp and host {} and port {}", ip, server.port);
    bpf_program b{};
    require(pcap_compile(h.p, &b, filter.c_str(), 1, PCAP_NETMASK_UNKNOWN) == 0, pcap_geterr(h.p));
    int r = pcap_setfilter(h.p, &b);
    pcap_freecode(&b);
    require(r == 0, pcap_geterr(h.p));
    require(pcap_setnonblock(h.p, 1, err) == 0, err);
    std::unique_ptr<pcap_dumper_t, decltype(&pcap_dump_close)> out(pcap_dump_open(h.p, file), pcap_dump_close);
    require(bool(out), pcap_geterr(h.p));
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    size_t count = 0, bytes = 0;
    std::cout << "CAPTURING\n" << std::flush;
    while (std::chrono::steady_clock::now() < end) {
        pcap_pkthdr *hdr;
        const unsigned char *data;
        int rc = pcap_next_ex(h.p, &hdr, &data);
        require(rc >= 0, pcap_geterr(h.p));
        if (rc) {
            bytes += hdr->caplen;
            require(bytes <= 256 * 1024 * 1024, "capture limit 256MiB");
            pcap_dump(reinterpret_cast<unsigned char *>(out.get()), hdr, data);
            ++count;
        } else
            poll(nullptr, 0, 1);
    }
    require(pcap_dump_flush(out.get()) == 0, "capture write failed");
    pcap_stat stats{};
    require(pcap_stats(h.p, &stats) == 0, "capture statistics unavailable");
    require(stats.ps_drop == 0 && stats.ps_ifdrop == 0, "capture dropped packets; recording unusable");
    std::cout << std::format("{{\"captured_packets\":{},\"dropped\":{}}}\n", count, stats.ps_drop);
}
int gateway_tcp_rr_main(int argc, char **argv) {
    try {
        require(argc >= 2, "record IFACE SERVER_IP PORT PCAP SECONDS | replay-client/replay-server PCAP ORIGINAL_IP "
                           "PORT TARGET_IP PORT | inspect PCAP SERVER_IP PORT");
        umask(0077);
        std::string mode = argv[1];
        if (mode == "record") {
            require(argc == 7, "record argument count");
            size_t end{};
            auto seconds = std::stoul(argv[6], &end);
            require(end == strlen(argv[6]) && seconds > 0 && seconds <= 3600, "invalid capture duration");
            record(argv[2], endpoint(argv[3], argv[4]), argv[5], seconds);
        } else if (mode == "inspect") {
            require(argc == 5, "inspect argument count");
            auto t = load(argv[2], endpoint(argv[3], argv[4]));
            std::cout << std::format("{{\"client_bytes\":{},\"server_bytes\":{},\"events\":{},\"duplicates\":{}}}\n",
                                     t.streams[0].bytes.size(), t.streams[1].bytes.size(), t.events.size(),
                                     t.duplicates);
        } else {
            require((mode == "replay-client" || mode == "replay-server") && argc == 7, "replay argument count");
            auto t = load(argv[2], endpoint(argv[3], argv[4]));
            replay(t, endpoint(argv[5], argv[6]), mode == "replay-server");
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "gateway: " << e.what() << '\n';
        return 1;
    }
}
