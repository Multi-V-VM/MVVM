#include "logging.h"
#include <pcap.h>

void packet_handler(u_char *user, const struct pcap_pkthdr *header, const u_char *packet) {
    // Analyze packet

    // Modify packet if needed

    // Forward packet using raw socket
}

void keep_alive(int socket) {
    // Send keep alive message to socket
}

int main() {
    pcap_t *handle;
    int new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program fp {};
    int server_fd = socket(AF_INET, SOCK_STREAM, 0); // Create a socket
    // ... code to set up the socket address structure and bind the socket ...

    // Forcefully attaching socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = MVVM_SOCK_ADDR;
    address.sin_port = htons(MVVM_SOCK_PORT);

    // Bind the socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Accept an incoming connection
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    while (true) { // Open the device for sniffing
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        handle = pcap_open_live("eth0", BUFSIZ, 1, 1000, errbuf);

        // Compile and apply the filter
        pcap_compile(handle, &fp, "tcp", 0, PCAP_NETMASK_UNKNOWN);
        pcap_setfilter(handle, &fp);

        // Capture packets
        pcap_loop(handle, -1, packet_handler, nullptr);

        pcap_close(handle);

        // keep alive to socket

        // redirect to new client
        close(new_socket);
        close(server_fd);
    }
    return 0;
}