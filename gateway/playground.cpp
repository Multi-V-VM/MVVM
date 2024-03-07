/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  Copyright 2023 Regents of the Univeristy of California
 *  UC Santa Cruz Sluglab.
 */

#include <crafter.h>
#include <crafter/Utils/TCPConnection.h>
#include <iostream>
#include <string>
#include <thread>
#include <coroutine>
#include <exception>

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

template<typename T>
struct task;

// Specialization for void
template<>
struct task<void> {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;

    task(handle_type h) : coro(h) {}
    ~task() { if (coro) coro.destroy(); }

    void get() {
        if (coro) {
            coro.resume();
            if (coro.done()) coro.promise().rethrow_if_exception();
        }
    }

    struct promise_type {
        std::exception_ptr exception;

        auto get_return_object() { return task{handle_type::from_promise(*this)}; }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {} // Adjusted for void
        void unhandled_exception() { exception = std::current_exception(); }

        void rethrow_if_exception() {
            if (exception) std::rethrow_exception(exception);
        }
    };
};

class SyncOperation {
public:
    explicit SyncOperation(TCPConnection& conn,TCPConnection& conn2 ) : _conn(conn),_conn2(conn2) {}

    // Check if the operation is already complete (e.g., for immediate completion)
    bool await_ready() const noexcept {
        // Example condition, adapt based on your actual async operation
        return false;
    }

    // Called by the compiler if await_ready returns false; suspends the coroutine
    // std::coroutine_handle<> is a handle to the suspended coroutine
    void await_suspend(std::coroutine_handle<> handle) {
        // Initiate the async operation and provide a callback mechanism
        // that will resume the coroutine once the operation completes.

        auto payload = Payload();
        _conn.Read(payload);
        _conn2.Send(payload.GetRawPointer(), sizeof(*payload.GetRawPointer()));
        handle.resume();
    }

    // Called by the compiler when the coroutine is resumed; retrieves the result
    void await_resume() noexcept {
        // Perform any cleanup or result retrieval necessary
        // In this simple case, there's nothing to return or throw
    }

private:
    TCPConnection& _conn;
    TCPConnection& _conn2;
};


task<void> SyncAndBlockTraffic(string src_ip, string dst_ip, short_word dstport, string iface) {
    // Begin the spoofing
    ARPContext* arp_context = ARPSpoofingReply(dst_ip, src_ip, iface);
    PrintARPContext(*arp_context);

    string filter = "tcp and host " + dst_ip + " and host " + src_ip;
    filter += " and dst port " + to_string(dstport);

    // Launch the sniffer asynchronously
    Sniffer(filter, iface, PacketHandler);

    // TCP connections setup
    auto tcp_v_to_s = make_unique<TCPConnection>(src_ip, dst_ip, srcport, dstport, iface, TCPConnection::ESTABLISHED);
    auto tcp_s_to_v = make_unique<TCPConnection>(dst_ip, src_ip, dstport, srcport, iface, TCPConnection::ESTABLISHED);

    // Synchronize the ACK and SEQ numbers
    tcp_v_to_s->Sync();
    tcp_s_to_v->Sync();

    // Blocking traffic and other operations can also be converted to async tasks
    start_block(dst_ip, src_ip, dstport, srcport);

    // Sending data, closing connections, etc., could be async as well
    co_await SyncOperation(*tcp_v_to_s, *tcp_s_to_v);
    co_await SyncOperation(*tcp_s_to_v, *tcp_v_to_s);
    tcp_v_to_s->Close();
    tcp_s_to_v->Close();
    // Cleanup
    clear_forward();
    CleanARPContext(arp_context);
}

int main() {
    auto task = SyncAndBlockTraffic("172.17.0.3", "172.17.0.2", 1234, "docker0");
    task.get(); // In a real application, you would likely not wait in the main thread.

    return 0;
}