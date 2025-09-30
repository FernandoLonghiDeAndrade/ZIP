#pragma once

#include <netinet/in.h>
#include <netdb.h>
#include <chrono>
#include <string>
#include <unistd.h>
#include "request.h"
#include "reply.h"

class ZIPClient {
public:
    // Constructors/Destructors

    ZIPClient(std::string server_port = "4000", std::string server_ip = "255.255.255.255");
    ~ZIPClient() { close(this->socket_fd); }

    // Public Methods

    /**
     * @brief ### Main client loop
     */
    void run();

private:
    // Member variables

    uint32_t balance = 0; // Client balance

    int32_t socket_fd; // Socket file descriptor
    struct sockaddr_in socket_addr; // Socket address structure
    uint16_t server_port; // Server port
    uint32_t server_ip; // Server IP address

    int32_t seq_number = 0; // Sequence number for requests

    std::chrono::steady_clock::time_point start_time; // Timer start point
    std::chrono::milliseconds timeout_duration = std::chrono::milliseconds(500); // Timeout duration

    // Private Methods

    /**
     * @brief ### Connects to the server by performing a discovery request
     */
    void connect_to_server();

    /**
     * @brief ### Sends a request to the server and waits for its reply, with retransmission on timeout
     * @param req Request to send
     * @return Reply from server
     */
    Reply send_request_and_wait(const Request& req);

    /**
     * @brief ### Sends a request to the server (does not wait for reply)
     * @param req Request to send
     */
    void send_request(const Request& req);

    /**
     * @brief ### Receives a reply from the server (nonblocking)
     * @return Reply from server (-1 in seq_number if no valid reply)
     */
    Reply receive_reply();

    /**
     * @brief ### Transfers value to dst_ip and updates balance
     * @param dst_ip Destination IP address
     * @param value Value to transfer
     */
    void transfer(uint32_t dst_ip, uint32_t value);

    /**
     * @brief ### Starts the timeout timer
     */
    void start_timer();

    /**
     * @brief ### Checks if the timeout duration has been exceeded
     * @return true if timeout occurred, false otherwise
     */
    bool timeout();
};
