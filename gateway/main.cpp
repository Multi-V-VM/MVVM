#include "logging.h"
#include <crafter.h>
#include <pcap.h>

using namespace Crafter;

std::string client_ip;
std::string server_ip;

void packet_handler(u_char *user, const struct pcap_pkthdr *header, const u_char *packet) {
    // Analyze packet

    // Modify packet if needed

    // Forward packet using raw socket
}

void keep_alive(std::string source_ip, int source_port, std::string dest_ip, int dest_port) {
    // Send keep alive message to socket
    // Initialize Libcrafter
    InitCrafter();

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

    // Clean up Libcrafter
    CleanCrafter();
}

int main() {
    pcap_t *handle;
    int client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int rc;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program fp {};
    struct mvvm_op_data op_data;

    int fd = socket(AF_INET, SOCK_STREAM, 0); // Create a socket
    // ... code to set up the socket address structure and bind the socket ...

    // Forcefully attaching socket to the port
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(MVVM_SOCK_PORT);
    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, MVVM_SOCK_ADDR, &address.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the network address and port
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for connections
    if (listen(fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }


    while (true) { // Open the device for sniffing
        perror("accept");
        client_fd = accept(fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_fd < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        // offload info from client
        if ((rc = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
            memcpy(&op_data, buffer, sizeof(op_data));
            switch (op_data.op) {
            case MVVM_SOCK_SUSPEND:
                // suspend
                perror("suspend");
                fprintf(stderr, "%d, %d, %d, %d", op_data.server_ip, op_data.server_port, op_data.client_ip,
                        op_data.client_port);
                server_ip =
                    fmt::format("%d.%d.%d.%d", (op_data.server_ip >> 24) & 0xFF, (op_data.server_ip >> 16) & 0xFF,
                                (op_data.server_ip >> 8) & 0xFF, (op_data.server_ip) & 0xFF);
                client_ip =
                    fmt::format("%d.%d.%d.%d", (op_data.client_ip >> 24) & 0xFF, (op_data.client_ip >> 16) & 0xFF,
                                (op_data.client_ip >> 8) & 0xFF, (op_data.client_ip) & 0xFF);
                keep_alive(server_ip, op_data.server_port, client_ip, op_data.client_port);

                handle = pcap_open_live(MVVM_SOCK_INTERFACE, BUFSIZ, 1, 1000, errbuf);

                // Compile and apply the filter
                pcap_compile(handle, &fp, "tcp", 0, PCAP_NETMASK_UNKNOWN);
                pcap_setfilter(handle, &fp);

                // Capture packets
                pcap_loop(handle, -1, packet_handler, nullptr);

                pcap_close(handle);

                // keep alive to socket

                // redirect to new client
                close(client_fd);
                close(fd);
                break;
            case MVVM_SOCK_RESUME:
                // resume
                perror("resume");
                break;
            }
        }
    }
    return 0;
}