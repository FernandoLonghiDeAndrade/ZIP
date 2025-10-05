#include "print_utils.h"
#include "udp_socket.h"
#include <iostream>
#include <ctime>
#include <iomanip>

// ===== Helper functions =====

/**
 * @brief Prints current timestamp in format "YYYY-MM-DD HH:MM:SS" to stdout.
 * 
 * Uses localtime() to convert UTC time to system timezone.
 * Thread-safe note: localtime() is NOT thread-safe (uses static buffer).
 * Consider localtime_r() for multi-threaded logging if needed.
 */
static void print_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    std::cout << std::put_time(local_time, "%Y-%m-%d %H:%M:%S");    
}

// ===== Print utilities =====

void PrintUtils::print_server_state(uint32_t num_transactions, uint64_t total_transferred, uint64_t total_balance) {
    print_timestamp();
    std::cout << " num_transactions " << num_transactions
              << " total_transferred " << total_transferred
              << " total_balance " << total_balance << std::endl;
}

void PrintUtils::print_request(uint32_t client_ip, const Packet& packet, bool is_duplicate, uint32_t num_transactions, uint64_t total_transferred, uint64_t total_balance) {
    // Line 1: Transaction details with duplicate marker if applicable
    print_timestamp();
    std::cout << " client " << UDPSocket::ip_to_string(htonl(client_ip))  // Convert host -> network byte order for display
              << (is_duplicate ? " DUP!!" : "")                // Mark retransmissions
              << " id_req " << packet.request_id
              << " dest " << UDPSocket::ip_to_string(packet.payload.request.destination_ip)  // Already in network byte order
              << " value " << packet.payload.request.value << std::endl;
    
    // Line 2: Current bank statistics (unchanged if duplicate)
    std::cout << "num_transactions " << num_transactions
              << " total_transferred " << total_transferred
              << " total_balance " << total_balance << std::endl;
}

void PrintUtils::print_reply(uint32_t server_ip, uint32_t request_id, uint32_t dest_ip, uint32_t value, uint32_t new_balance) {
    // Single line: successful transaction confirmation with updated balance
    print_timestamp();
    std::cout << " server " << UDPSocket::ip_to_string(server_ip)      // Already in network byte order
              << " id_req " << request_id
              << " dest " << UDPSocket::ip_to_string(dest_ip)          // Already in network byte order
              << " value " << value 
              << " new_balance " << new_balance << std::endl << std::endl;  // Extra newline for readability
}

void PrintUtils::print_discovery_reply(uint32_t server_ip) {
    // Single line: discovery phase completed
    print_timestamp();
    std::cout << " server_addr " << UDPSocket::ip_to_string(server_ip) << std::endl << std::endl;  // Extra newline for readability
}
