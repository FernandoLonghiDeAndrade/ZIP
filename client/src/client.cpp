
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <chrono> 
#include <thread> 
#include <iostream>
#include <cstdint>

#include "client.h"

Client::Client() {
	// Create socket
	if ((this->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		std::cerr << "ERROR opening socket" << std::endl;
		exit(1);
    } else {
        std::cout << "Socket successfully created" << std::endl;
    }
	// Initialize timer
	this->start_timer();
	this->connect_to_server(); // Placeholder, will be set after discovery
}

int32_t Client::transfer(int32_t dest_addr, int32_t value) {
	Request req = Request();
	req.type = RequestType::TRANSFER;
	req.seq_number = this->seq_number;
	req.dest_addr = dest_addr;
	req.value = value;
	Reply reply = this->request_and_wait(req);
	//2024-10-01 18:37:01 server 10.1.1.20 id req 1 dest 10.1.1.3 value 10 new balance 90
	this->print_time();
	std::cout << " server " << inet_ntoa(this->server_addr.sin_addr);
	std::cout << " id req " << reply.seq_number;
	std::cout << " dest " << inet_ntoa(gen_server_addr(req.dest_addr, 0).sin_addr);
	std::cout << " value " << req.value;
	std::cout << " new balance " << reply.new_balance << std::endl;
	return reply.new_balance;
}


Reply Client::request_and_wait(Request req) {
	// Initialize request and reply
	Reply reply = Reply();
	// Wait for reply with retransmissions
	while (true) {
		// Check timeout
		if (this->timeout()) {
			this->send_request(req);
			this->start_timer();
		}
		// Receive ack
		reply = this->receive_reply();
		if (reply.seq_number == this->seq_number) {
			this->seq_number++;
			break;
		}
		// Sleep for a short duration to avoid busy waiting
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	return reply;
}

// Local network broadcast discovery to find server ip and port
Reply Client::discovery() {
	Request req = Request();
	req.type = RequestType::DISCOVERY;
	req.seq_number = this->seq_number;
	Reply reply = this->request_and_wait(req);
	return reply;
}

void Client::send_request(Request req) {
	if (req.type == RequestType::TRANSFER) {
		sendto(this->sockfd, &req, sizeof(req), 0, (struct sockaddr *) &this->server_addr, sizeof(struct sockaddr));
	} else if (req.type == RequestType::DISCOVERY) {
		// For discovery, send to broadcast address
		struct sockaddr_in broadcast_addr = this->gen_broadcast_addr();
		sendto(this->sockfd, &req, sizeof(req), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
	} else {
		std::cerr << "ERROR unknown request type" << std::endl;
		exit(1);
	}
}

Reply Client::receive_reply() {
	Reply reply = Reply();
	struct sockaddr_in from;
	socklen_t fromlen = sizeof(from);
	// Use MSG_DONTWAIT to avoid blocking
	int bytes_received = recvfrom(this->sockfd, &reply, sizeof(reply), MSG_DONTWAIT, (struct sockaddr *) &from, &fromlen);
	if (bytes_received <= 0) {
		// No data received, return reply with seq_number -1 to indicate failure
		reply.seq_number = -1;
	}
	return reply;
}

void Client::connect_to_server() {
	std::cout << "Connecting to server..." << std::endl;
	// Find server address via discovery
	Reply discovery_reply = this->discovery();
	int32_t server_ip = discovery_reply.server_ip;
	int32_t server_port = discovery_reply.server_port;	
	// Initialize server 
	this->server_addr = gen_server_addr(server_ip, server_port);
	this->print_time();
	std::cout << " Connected to server at " << inet_ntoa(this->server_addr.sin_addr) << ":" << ntohs(this->server_addr.sin_port) << std::endl;
}

void Client::start_timer() {
	this->start_time = std::chrono::steady_clock::now();
}

// Format: 2024-10-01 18:37:02
void Client::print_time() {
    // Get current time
    time_t now = time(0);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    std::cout << buf;
}

bool Client::timeout() {
	auto current_time = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - this->start_time);
	return elapsed >= this->timeout_duration;
}

Client::~Client() {
    close(this->sockfd);
}

struct sockaddr_in Client::gen_broadcast_addr() {
	struct sockaddr_in broadcast_addr;
	memset(&broadcast_addr, 0, sizeof(broadcast_addr));
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_port = htons(DISCOVERY_PORT);
	// Use localhost instead of broadcast for testing
	broadcast_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	return broadcast_addr;
}

struct sockaddr_in Client::gen_server_addr(int32_t server_ip, int16_t server_port) {
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);
	server_addr.sin_addr.s_addr = htonl(server_ip);
	return server_addr;
}

