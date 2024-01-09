//
// Created by victoryang00 on 5/6/23.
//

#include "logging.h"

void LogWriter::operator<(const LogStream &stream) {
    std::ostringstream msg;
    msg << stream.sstream_->rdbuf();
    output_log(msg);
}

void LogWriter::output_log(const std::ostringstream &msg) {
    if (log_level_ >= env_log_level)
        std::cout << fmt::format(fmt::emphasis::bold | fg(level2color(log_level_)), "[{}] ({}:{} L {}) ",
                                 level2string(log_level_), location_.file_, location_.line_, location_.func_)
                  << fmt::format(fg(level2color(log_level_)), "{}", msg.str()) << std::endl;
}
std::string level2string(LogLevel level) {
    switch (level) {
    case BH_LOG_LEVEL_DEBUG:
        return "DEBUG";
    case BH_LOG_LEVEL_VERBOSE:
        return "INFO";
    case BH_LOG_LEVEL_FATAL:
        return "FATAL";
    case BH_LOG_LEVEL_WARNING:
        return "WARNING";
    case BH_LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "";
    }
}
fmt::color level2color(LogLevel level) {
    switch (level) {
    case BH_LOG_LEVEL_DEBUG:
        return fmt::color::alice_blue;
    case BH_LOG_LEVEL_VERBOSE:
        return fmt::color::magenta;
    case BH_LOG_LEVEL_FATAL:
        return fmt::color::rebecca_purple;
    case BH_LOG_LEVEL_WARNING:
        return fmt::color::yellow;
    case BH_LOG_LEVEL_ERROR:
        return fmt::color::red;
    default:
        return fmt::color::white;
    }
}

bool is_ip_in_cidr(const char *base_ip, int subnet_mask_len, uint32_t ip) {
    uint32_t base_ip_bin, subnet_mask, network_addr, broadcast_addr;
    LOGV(DEBUG) << "base_ip: " << base_ip << " subnet_mask_len: " << subnet_mask_len << "ip: "
                << fmt::format("{}.{}.{}.{}", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);

    // Convert base IP to binary
    if (inet_pton(AF_INET, base_ip, &base_ip_bin) != 1) {
        fprintf(stderr, "Error converting base IP to binary\n");
        return false;
    }

    // Ensure that the subnet mask length is valid
    if (subnet_mask_len < 0 || subnet_mask_len > 32) {
        fprintf(stderr, "Invalid subnet mask length\n");
        return false;
    }

    // Calculate subnet mask in binary
    subnet_mask = htonl(~((1 << (32 - subnet_mask_len)) - 1));

    // Calculate network and broadcast addresses
    network_addr = base_ip_bin & subnet_mask;
    broadcast_addr = network_addr | ~subnet_mask;

    // Ensure ip is in network byte order
    uint32_t ip_net_order = htonl(ip);

    // Check if IP is within range
    return ip_net_order >= network_addr && ip_net_order <= broadcast_addr;
}
bool is_ipv6_in_cidr(const char *base_ip_str, int subnet_mask_len, struct in6_addr *ip) {
    struct in6_addr base_ip {
    }, subnet_mask{}, network_addr{}, ip_min{}, ip_max{};
    unsigned char mask;

    // Convert base IP to binary
    inet_pton(AF_INET6, base_ip_str, &base_ip);

    // Clear subnet_mask and network_addr
    memset(&subnet_mask, 0, sizeof(subnet_mask));
    memset(&network_addr, 0, sizeof(network_addr));

    // Create the subnet mask and network address
    for (int i = 0; i < subnet_mask_len / 8; i++) {
        subnet_mask.s6_addr[i] = 0xff;
    }
    if (subnet_mask_len % 8) {
        mask = (0xff << (8 - (subnet_mask_len % 8)));
        subnet_mask.s6_addr[subnet_mask_len / 8] = mask;
    }

    // Apply the subnet mask to the base IP to get the network address
    for (int i = 0; i < 16; i++) {
        network_addr.s6_addr[i] = base_ip.s6_addr[i] & subnet_mask.s6_addr[i];
    }

    // Calculate the first and last IPs in the range
    ip_min = network_addr;
    ip_max = network_addr;
    for (int i = 15; i >= subnet_mask_len / 8; i--) {
        ip_max.s6_addr[i] = 0xff;
    }

    // Check if IP is within range
    for (int i = 0; i < 16; i++) {
        if (ip->s6_addr[i] < ip_min.s6_addr[i] || ip->s6_addr[i] > ip_max.s6_addr[i]) {
            return false;
        }
    }
    return true;
}
