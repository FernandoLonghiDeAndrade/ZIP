#include "print_utils.h"

std::string PrintUtils::ip_to_string(uint32_t ip_addr) {
    struct in_addr addr;
    addr.s_addr = htonl(ip_addr);
    return std::string(inet_ntoa(addr));
}

void PrintUtils::print_time() {
    time_t now = time(0);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    std::cout << buf;
}

void PrintUtils::print_server_state(uint32_t transactions, uint32_t transferred, uint32_t balance) {
    std::cout << " num transactions " << transactions
              << " total transferred " << transferred  
              << " total balance " << balance << std::endl;
}

void PrintUtils::print_transfer(uint32_t server_ip, uint32_t seq_number, uint32_t dst_ip, uint32_t value, uint32_t new_balance) {
    print_time();
    std::cout << " server " << ip_to_string(server_ip)
              << " id req " << seq_number
              << " dest " << ip_to_string(dst_ip)
              << " value " << value
              << " new balance " << new_balance << std::endl;
}

void PrintUtils::print_request(const Request& req, bool is_duplicate) {
    print_time();
    std::cout << " client " << ip_to_string(req.src_ip);
    if (is_duplicate) {
        std::cout << " DUP!!";
    }
    std::cout   << " id req " << req.seq_number
                << " dest " << ip_to_string(req.dst_ip)
                << " value " << req.value << std::endl;
}
