
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <thread>
#include <iostream>
#include <cstdint>
#include <mutex>
#include "server.h"

Server::Server(int16_t port) {
    // Initialize server state
    this->total_transactions = 0;
    this->total_transferred = 0;
    this->total_balance = 1000; // Initial balance
    
    // Create socket
    if ((this->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    	std::cout << "ERROR opening socket" << std::endl;
        exit(1);
    } else {
        std::cout << "Socket successfully created" << std::endl;
    }
    // initialize server address struct
	this->serv_addr = this->gen_addr(port); // Bind to all interfaces on given port
    // Bind socket
	if (bind(sockfd, (struct sockaddr *) &this->serv_addr, sizeof(struct sockaddr)) < 0) {
        std::cout << "ERROR on binding" << std::endl;
        exit(1);
    }
    else {
        std::cout << "Socket successfully binded" << std::endl;
    }
    this->print_time();
    this->print_server_state();
}

// Format: 2024-10-01 18:37:02
void Server::print_time() {
    // Get current time
    time_t now = time(0);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    std::cout << buf;
}

void Server::run() {
    while (true) {
        Request req = receive_request();
        // Process request in a new thread
        std::thread t(&Server::process, this, req);
        // Detach the thread to allow it to run independently
        t.detach();
    }
}

void Server::process(Request req) {
    Reply reply;

    if (req.type == RequestType::DISCOVERY) {
        reply = this->gen_discovery_reply(req);
    } else if (req.type == RequestType::TRANSFER) {
        // Implement transfer logic here
        reply = this->gen_discovery_reply(req); // Placeholder function
        // Do the processing
        // ...
        // For now, just set a dummy new balance
        reply.new_balance = 100; 
        
        // Protect shared state variables
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->total_transactions++;
            this->total_transferred += req.value;
            this->total_balance -= req.value; // Subtract transferred amount
        }
        
        this->print_request(req);
        this->print_server_state();
    } else {
        std::cerr << "ERROR unknown request type" << std::endl;
        return;
    }
    
    this->send_reply(reply, req.src_ip, req.src_port);
    
    // Protect reply_map access
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->reply_map[req.src_ip] = reply;
    }
}

// Format: num transactions 2 total transferred 18 total balance 300
void Server::print_server_state() {
    std::lock_guard<std::mutex> lock(this->mutex);
    std::cout << " num transactions " << this->total_transactions;
    std::cout << " total transferred " << this->total_transferred;
    std::cout << " total balance " << this->total_balance << std::endl;
}

Server::~Server() {
    close(sockfd);
}

Request Server::wait_for_new_request() {
    Request req;
	while (true) {
		// Read a p from the socket
        req = this->receive_request(); 
		// If the request is a retransmission, resend the corresponding reply
        if (this->is_duplicate(req)) { 
            Reply reply;
            {
                std::lock_guard<std::mutex> lock(this->mutex);
                reply = this->reply_map[req.src_ip];
            }
            this->send_reply(reply, req.src_ip, req.src_port);
            this->print_request(req);
            this->print_server_state();
        } else {
            break;
        }
	}
	return req;
}

Request Server::receive_request() {
    Request req;
    struct sockaddr_in sender_addr; 
    socklen_t sender_addr_len = sizeof(sender_addr);
    recvfrom(this->sockfd, &req, sizeof(req), 0, (struct sockaddr *) &sender_addr, &sender_addr_len);
    req.src_ip = ntohl(sender_addr.sin_addr.s_addr);
    req.src_port = ntohs(sender_addr.sin_port);
    return req;
}

void Server::print_request(Request req) {
    // Get current time
    time_t now = time(0);
    char* dt = ctime(&now);
    dt[strlen(dt)-1] = '\0'; // Remove newline
    // Convert IPs to string
    struct in_addr src_addr, dest_addr;
    src_addr.s_addr = htonl(req.src_ip);
    dest_addr.s_addr = htonl(req.dest_addr);
    // Print request
    std::cout << dt << " client " << inet_ntoa(src_addr);
    if (this->is_duplicate(req)) {
        std::cout << " DUP!!";
    }
    std::cout << " id req " << req.seq_number << " dest " << inet_ntoa(dest_addr) << " value " << req.value << std::endl;
}

bool Server::is_duplicate(Request req) {
    std::lock_guard<std::mutex> lock(this->mutex);
	if (this->reply_map.find(req.src_ip) != this->reply_map.end()) {
        Reply last_reply = this->reply_map[req.src_ip];
        if (req.seq_number <= last_reply.seq_number) {
            return true;
        }
    }
    return false;
}

void Server::send_reply(Reply reply, int32_t client_ip, int16_t client_port) {
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(client_ip);
    client_addr.sin_port = htons(client_port);
    bzero(&(client_addr.sin_zero), 8);
	sendto(this->sockfd, &reply, sizeof(reply), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
}

struct sockaddr_in Server::gen_addr(int16_t port) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces
    addr.sin_port = htons(port);
    bzero(&(addr.sin_zero), 8);
    return addr;
}

Reply Server::gen_discovery_reply(Request req) {
    Reply reply = Reply();
    // Since server binds to INADDR_ANY, return localhost for discovery
    reply.server_ip = ntohl(inet_addr("127.0.0.1"));
    reply.server_port = ntohs(this->serv_addr.sin_port);
    reply.seq_number = req.seq_number;
    reply.new_balance = 0; // Discovery does not affect balance
    return reply;
}
