#include "zip_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <thread>
#include <iostream>
#include <ifaddrs.h>
#include "print_utils.h"

ZIPServer::ZIPServer(std::string server_port) {
    // Initialize socket
    memset(&this->socket_addr, 0, sizeof(socket_addr));
    this->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (this->socket_fd == -1) {
        std::cerr << "ERROR opening socket" << std::endl;
        exit(1);
    }
    // Enable broadcast reception
    int broadcast = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        std::cerr << "ERROR enabling broadcast reception" << std::endl;
        exit(1);
    }
    // 
    this->socket_addr.sin_family = AF_INET;
    this->socket_addr.sin_addr.s_addr = INADDR_ANY;
    this->socket_addr.sin_port = htons(std::stoi(server_port));
    memset(&(this->socket_addr.sin_zero), 0, 8);

    if (bind(socket_fd, (struct sockaddr *) &this->socket_addr, sizeof(struct sockaddr)) < 0) {
        std::cerr << "ERROR on binding" << std::endl;
        exit(1);
    }
    PrintUtils::print_time();
    PrintUtils::print_server_state(this->total_transactions, this->total_transferred, this->total_balance);
}

void ZIPServer::run() {
    while (true) {
        Request req = wait_for_new_request();
        this->process_request(req);
    }
}

Request ZIPServer::wait_for_new_request() {
    // Waits until a new (non-duplicate) request is received
    while (true) {
        Request req = this->receive_request(); 
        ClientInfo client_info = this->client_map[req.src_ip];    
        bool is_duplicate = req.seq_number <= client_info.seq_number;
        // If duplicate, resend last reply. Else, return the request to the application
        if (is_duplicate) {
            Reply reply;
            reply.seq_number = req.seq_number;
            reply.new_balance = client_info.balance;
            this->send_reply(reply, req.src_ip, req.src_port);
            if (req.type != RequestType::DISCOVERY)
                PrintUtils::print_request(req, is_duplicate);
        } else {
            return req;
        }
    }
}

void ZIPServer::process_request(const Request& req) {
    // Log the request
    PrintUtils::print_request(req, false);
    // Process the request
    this->transactions.push_back({req.src_ip, req.seq_number, req.dst_ip, req.value});
    this->total_transactions++;
    this->total_transferred += req.value;
    this->total_balance -= req.value;
    // Generate and send reply
    Reply reply;
    reply.new_balance = 100;
    reply.seq_number = req.seq_number;
    this->send_reply(reply, req.src_ip, req.src_port);
    this->client_map[req.src_ip] = {req.seq_number, reply.new_balance};
}

Request ZIPServer::receive_request() {
    Request req;
    struct sockaddr_in sender_addr; 
    socklen_t sender_addr_len = sizeof(sender_addr);
    recvfrom(this->socket_fd, &req, sizeof(req), 0, (struct sockaddr *) &sender_addr, &sender_addr_len);
    req.src_ip = ntohl(sender_addr.sin_addr.s_addr);
    req.src_port = ntohs(sender_addr.sin_port);
    return req;
}

void ZIPServer::send_reply(const Reply& reply, int32_t client_ip, int16_t client_port) {
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(client_ip);
    client_addr.sin_port = htons(client_port);
    bzero(&(client_addr.sin_zero), 8);
    sendto(this->socket_fd, &reply, sizeof(reply), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
}
