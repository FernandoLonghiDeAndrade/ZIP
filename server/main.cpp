#include "server.h"
#include <iostream>
#include <cstdint>

/**
 * @brief Server entry point - starts multi-threaded UDP server.
 * 
 * Usage: ./server <port>
 * Example: ./server 8080
 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    // Parse and validate port
    uint16_t port = 0;
    try {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
        if (port == 0) {
            std::cerr << "Error: Port must be in range 1-65535" << std::endl;
            return 1;
        }
    } catch (const std::exception&) {
        std::cerr << "Error: Invalid port number" << std::endl;
        return 1;
    }

    // Start server
    try {
        Server server(port);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
