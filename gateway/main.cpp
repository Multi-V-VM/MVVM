#include <pcap.h>

void packetHandler(u_char *userData, const struct pcap_pkthdr* pkthdr, const u_char* packet) {
    // Process each packet here
}

int main() {
    char *dev;
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *descr;
    struct bpf_program fp;
    bpf_u_int32 maskp;
    bpf_u_int32 netp;

    // Get the name of the first device suitable for capture
    dev = pcap_lookupdev(errbuf);

    // Open the device for sniffing
    descr = pcap_open_live(dev, BUFSIZ, 1, -1, errbuf);

    // Compile and apply the filter
    pcap_compile(descr, &fp, "host 192.168.1.2 and 192.168.1.3", 0, netp);
    pcap_setfilter(descr, &fp);

    // Capture packets
    pcap_loop(descr, -1, packetHandler, NULL);

    return 0;
}