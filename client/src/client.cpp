#include "client.h"
#include "print_utils.h"
#include <iostream>
#include <sstream>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
#endif

// ===== Constructor =====

Client::Client(uint16_t server_port, const std::string& server_ip)
    : server_port(server_port), has_server_address(false), next_request_id(1) {
    pending_ack_request_id.store(0); // 0 indicates no pending request
    server_addr = sockaddr_in{};
    
    // Pre-configure server address if known IP provided (skips broadcast discovery)
    if (!server_ip.empty()) {
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        
        if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) == 1) {
            has_server_address = true; // Will use connect_to_known_server() instead of discover_server()
        } else {
            has_server_address = false; // Invalid IP format, fall back to broadcast discovery
        }
    }
}

// ===== Main execution =====

void Client::run() {
    // Enable broadcast capability for discovery phase (broadcasts to 255.255.255.255)
    if (!client_socket.initialize(0, true)) {
        std::cerr << "Failed to initialize client socket." << std::endl;
        return;
    }

    // Phase 1: Discover server (either broadcast or direct connection)
    if (has_server_address) {
        connect_to_known_server(); // Send DISCOVERY to specific IP
    } else {
        discover_server(); // Broadcast DISCOVERY to 255.255.255.255
    }
    
    // Phase 2: Spawn network thread to listen for ACKs asynchronously
    // Main thread will block on user input, network thread handles responses in parallel
    network_thread = std::thread(&Client::handle_server_responses, this);
    
    // Phase 3: Main thread handles user input (blocks here indefinitely)
    run_user_input_loop();

    // Cleanup: detach network thread to allow main thread exit without waiting
    // Alternative: could join() if implementing graceful shutdown
    if (network_thread.joinable()) {
        network_thread.detach(); 
    }
}

// ===== Server discovery =====

void Client::discover_server() {
    // Discovery packet has request_id = 0 (special value, not counted in next_request_id)
    Packet discovery_packet;
    discovery_packet.type = DISCOVERY;
    discovery_packet.request_id = 0;
    
    // Broadcast to all devices on local network (255.255.255.255)
    struct sockaddr_in broadcast_addr;
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, "255.255.255.255", &broadcast_addr.sin_addr);

    // Retry loop: send DISCOVERY every ACK_TIMEOUT_MS until server responds
    while (!has_server_address) {
        client_socket.send(&discovery_packet, sizeof(Packet), broadcast_addr);

        // Non-blocking wait: allows retransmission if no response within timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(ACK_TIMEOUT_MS));
        
        // Check if DISCOVERY_ACK arrived during sleep (non-blocking receive)
        Packet response_packet;
        struct sockaddr_in received_from_addr;
        if (client_socket.receive(&response_packet, sizeof(Packet), received_from_addr) > 0) {
            if (response_packet.type == DISCOVERY_ACK) {
                // Success: store server's address for future transactions
                this->server_addr = received_from_addr;
                this->has_server_address = true;
                PrintUtils::print_discovery_reply(server_addr.sin_addr.s_addr);
                return;
            }
        }
        // If no response or wrong type, loop continues and retransmits
    }
}

void Client::connect_to_known_server() {
    // Same as discover_server() but sends to specific IP instead of broadcast
    Packet discovery_packet;
    discovery_packet.type = DISCOVERY;
    discovery_packet.request_id = 0;

    // Retry loop: server might not be ready yet or packets might be lost
    bool received_ack = false;
    while (!received_ack) {
        client_socket.send(&discovery_packet, sizeof(Packet), server_addr);
        
        // Wait for DISCOVERY_ACK from the specific server IP
        std::this_thread::sleep_for(std::chrono::milliseconds(ACK_TIMEOUT_MS));
        
        Packet response_packet;
        struct sockaddr_in received_from_addr;
        if (client_socket.receive(&response_packet, sizeof(Packet), received_from_addr) > 0) {
            if (response_packet.type == DISCOVERY_ACK) {
                // Verify response came from expected server (could add IP validation here)
                this->server_addr = received_from_addr;
                received_ack = true;
                PrintUtils::print_discovery_reply(server_addr.sin_addr.s_addr);
            }
        }
        // If no response, loop continues and retransmits
    }
}

// ===== User input handling =====

void Client::run_user_input_loop() {
    std::string line;

    while (true) {
        std::getline(std::cin, line);
        if (line.empty()) continue; // Ignore empty lines (user pressed Enter)

        // Parse input: "192.168.1.100 50" -> ip_str="192.168.1.100", value=50
        std::stringstream ss(line);
        std::string ip_str;
        int32_t value;
        ss >> ip_str >> value;

        // Validation: prevent negative values (could also check for overflow)
        if (value < 0) {
            std::cerr << "Value must be non-negative.\n\n";
            continue;
        }

        // Convert string IP to binary format (network byte order)
        struct in_addr dest_addr_struct;
        if (inet_pton(AF_INET, ip_str.c_str(), &dest_addr_struct) != 1) {
            std::cerr << "Invalid destination IP address format. Expected format: xxx.xxx.xxx.xxx\n\n";
            continue;
        }

        // Create packet and send with stop-and-wait retransmission
        Packet request_packet = Packet::create_request(TRANSACTION_REQUEST, next_request_id, dest_addr_struct.s_addr, value);
        send_request(request_packet); // Blocks until ACK received or send fails

        next_request_id++; // Increment for next transaction (wraps around at UINT32_MAX)
    }
}

// ===== Request transmission with stop-and-wait =====

void Client::send_request(const Packet& packet) {
    std::unique_lock<std::mutex> lock(pending_request_mutex); // Acquire lock for entire stop-and-wait cycle
    
    // Signal to network thread: "I'm waiting for ACK with this ID"
    pending_ack_request_id.store(packet.request_id);
    
    // Store packet for two reasons:
    // 1. Retransmission (if needed within this function)
    // 2. Printing reply in handle_server_responses() (after ACK arrives)
    pending_request_packet = packet;

    // Stop-and-wait loop: retransmit every ACK_TIMEOUT_MS until ACK arrives
    while (pending_ack_request_id.load() == packet.request_id) {
        // Send packet to server
        if (!client_socket.send(&packet, sizeof(Packet), server_addr)) {
            // Socket send failed (network error), abort this request
            pending_ack_request_id.store(0); // Clear pending state
            return;
        }

        // Wait for ACK with timeout:
        // - If ACK arrives: network thread calls notify_one(), this wakes up
        // - If timeout: spurious wakeup, loop condition checks if ACK arrived
        // - If ACK arrived: pending_ack_request_id is now 0, loop exits
        ack_received_cv.wait_for(lock, std::chrono::milliseconds(ACK_TIMEOUT_MS));
        
        // Loop continues if pending_ack_request_id still equals packet.request_id
        // (meaning ACK didn't arrive yet, so retransmit)
    }
    // Exit when pending_ack_request_id == 0 (set by network thread upon ACK)
}

// ===== Response handling thread =====

void Client::handle_server_responses() {
    Packet response_packet;
    struct sockaddr_in sender_addr;

    while (true) {
        // Blocking receive: wait indefinitely for next packet from server
        // Socket is in blocking mode, so this doesn't spin CPU
        int32_t bytes_received;
        do {
            bytes_received = client_socket.receive(&response_packet, sizeof(Packet), sender_addr);
        } while (bytes_received < 1); // Retry if receive fails (shouldn't happen in blocking mode)
        
        // Fast path check: is this ACK for the current pending request?
        // Uses atomic load WITHOUT mutex for performance (hot path)
        if (response_packet.request_id == pending_ack_request_id.load()) {
            {
                // Acquire mutex to safely clear pending state
                std::lock_guard<std::mutex> lock(pending_request_mutex);
                pending_ack_request_id.store(0); // Signal: "ACK received, stop retransmitting"
            }
            // Wake up send_request() which is waiting on ack_received_cv
            ack_received_cv.notify_one();

            // Process different ACK types (all mean request was processed, but with different results)
            switch (response_packet.type) {
                case TRANSACTION_ACK:
                    // Success: print transaction details and new balance
                    PrintUtils::print_reply(
                        sender_addr.sin_addr.s_addr,
                        pending_request_packet.request_id,
                        pending_request_packet.payload.request.destination_ip,
                        pending_request_packet.payload.request.value,
                        response_packet.payload.reply.new_balance
                    );
                    break;
                case INSUFFICIENT_BALANCE_ACK:
                    std::cout << "Transaction failed: Insufficient balance.\n\n";
                    break;
                case INVALID_CLIENT_ACK:
                    std::cout << "Transaction failed: Invalid destination client.\n\n";
                    break;
                case ERROR_ACK:
                    std::cout << "Transaction failed: Server error.\n\n";
                    break;
            }
        }
        // If request_id doesn't match: ignore packet (duplicate ACK from previous request or out-of-order)
    }
}
