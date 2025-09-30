#pragma once

#include <netinet/in.h>
#include <unordered_map>
#include <list>
#include <mutex>
#include <string>
#include <unistd.h>
#include "request.h"
#include "reply.h"

struct ClientInfo {
    int32_t seq_number;
    uint32_t balance;
};

struct Transaction {
    uint32_t src_ip;
    int32_t seq_number;
    uint32_t dst_ip;
    int32_t value;
};

class ZIPServer {
public:
    // Constructors/Destructors

    ZIPServer(std::string server_port = "4000");
    ~ZIPServer() { close(socket_fd); }

    // Public Methods

    /**
     * @brief ### Main server loop
     */
    void run();

    // Static Constants

    static constexpr uint32_t INITIAL_BALANCE = 1000; // Client's initial balance

private:
    // Member Variables

    std::list<Transaction> transactions; // Transaction history
    std::unordered_map<uint32_t, ClientInfo> client_map; // Map of clients. Maps client IP to ClientInfo

    uint32_t total_transactions = 0; // Total transactions
    uint32_t total_transferred = 0; // Total transferred amount
    uint32_t total_balance = 0; // Total balance

    std::mutex mutex; // Mutex to protect access to shared state variables

    int32_t socket_fd; // Socket file descriptor
    struct sockaddr_in socket_addr; // Socket address structure

    void process_request(const Request& req);
    Request wait_for_new_request();
    Request receive_request();
    Reply gen_discovery_reply(const Request& req);
    void send_reply(const Reply& reply, int32_t client_addr, int16_t client_port);
    struct sockaddr_in gen_addr(int16_t port);
    uint32_t get_local_ip(); // Método para obter IP local do servidor
};