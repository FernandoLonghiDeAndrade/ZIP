#pragma once
#include <netinet/in.h>
#include <netdb.h>
#include <string>
#include <chrono>
#include "request.h"
#include "reply.h"

class Client {
public:
    Client();
    ~Client();
    int32_t transfer(int32_t dest_addr, int32_t value);
private:
    // Networking
    int sockfd;
    struct sockaddr_in server_addr;
    void connect_to_server();
    // Request/Reply
    struct sockaddr_in gen_broadcast_addr();
    struct sockaddr_in gen_server_addr(int32_t server_ip, int16_t server_port);
    int seq_number = 0;
    void send_request(Request req);
    void print_time();
    Reply discovery();
    Reply request_and_wait(Request req);
    Reply receive_reply();
    // Timing
    std::chrono::steady_clock::time_point start_time;
    std::chrono::seconds timeout_duration = std::chrono::seconds(1);
    void start_timer();
    bool timeout();
};

