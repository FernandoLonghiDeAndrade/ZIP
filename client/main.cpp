#include "client.h"
#include <iostream>
#include <cstdint>

/**
 * @brief Client entry point - connects to server and sends transactions.
 * 
 * Usage: ./client <server_port> [server_ip]
 * Examples:
 *   ./client 8080                  # Broadcast discovery
 *   ./client 8080 192.168.1.100    # Direct connection
 */
int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <server_port> [server_ip]" << std::endl;
        return 1;
    }

    // Parse port
    uint16_t server_port = 0;
    try {
        server_port = static_cast<uint16_t>(std::stoi(argv[1]));
        if (server_port == 0) {
            std::cerr << "Error: Port must be in range 1-65535" << std::endl;
            return 1;
        }
    } catch (const std::exception&) {
        std::cerr << "Error: Invalid port number" << std::endl;
        return 1;
    }

    // Start client
    try {
        Client client = (argc == 2) ? Client(server_port) : Client(server_port, argv[2]);
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
