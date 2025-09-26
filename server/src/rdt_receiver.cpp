
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include <pthread.h>
#include <iostream>
#include <cstdint>
#include <mutex>
#include "rdt_receiver.h"
#include "packet.h"

RDTReceiver::RDTReceiver(int port) {
    if ((this->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) 
    	printf("ERROR opening socket");

    printf("passou pela criacao do socket\n");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(serv_addr.sin_zero), 8);
	
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr)) < 0)
		printf("ERROR on binding");
	
	printf("passou pelo bind\n");
}

RDTReceiver::~RDTReceiver() {
    close(sockfd);
}

RDTMail RDTReceiver::get_mail() {

	std::unique_lock<std::mutex> lock(this->mutex);

	Packet request_packet(NULL, Request());
	Request req = Request();
	struct sockaddr_in sender_addr;
	socklen_t sender_addr_len = sizeof(sender_addr);
	int n = -1;
	RDTMail mail = RDTMail();

	while (true) {

		// Read a packet from the socket
		n = recvfrom(this->sockfd, &request_packet, sizeof(request_packet), 0, (struct sockaddr *) &sender_addr, &sender_addr_len);
		uint64_t sender_addr_key = get_sender_addr_key(sender_addr);
		// If the packet is a retransmission, resend the corresponding response
		if (n > 0) {
			if (is_retransmission(request_packet, sender_addr_key)) {
				send_response(this->request_map[sender_addr_key].data.resp, sender_addr_key);
			} else {
				// Otherwise, store the packet in the retransmission map and return the request to the application
				this->request_map[sender_addr_key] = request_packet;
				mail.sender_addr = sender_addr_key;
				mail.req = request_packet.data.req;
				break;
			}
		}
	}

	return mail;
	
}

bool RDTReceiver::is_retransmission(Packet packet, uint64_t sender_addr_key) {
	if (this->request_map.find(sender_addr_key) == this->request_map.end())
		return false;
	else 
		return (this->request_map[sender_addr_key].seq_number == packet.seq_number);
}

void RDTReceiver::send_response(Response resp, uint64_t sender_addr_key) {

	std::unique_lock<std::mutex> lock(this->mutex);
	// Convert sender addr key to struct sockaddr_in
	struct sockaddr_in sender_addr = get_sender_addr(sender_addr_key); 

	int seq_number = this->request_map[sender_addr_key].seq_number;
	Packet packet(seq_number, resp);
	sendto(this->sockfd, &packet, sizeof(packet), 0, (struct sockaddr *) &sender_addr, sizeof(sender_addr));

}

struct sockaddr_in RDTReceiver::get_sender_addr(uint64_t sender_addr_key) {
	struct sockaddr_in sender_addr;
	memset(&sender_addr, 0, sizeof(sender_addr));
	sender_addr.sin_family = AF_INET;
	// Extract IP from upper 32 bits and port from lower 16 bits
	sender_addr.sin_addr.s_addr = (uint32_t)(sender_addr_key >> 32);
	sender_addr.sin_port = htons((uint16_t)(sender_addr_key & 0xFFFF));
	return sender_addr;
}

uint64_t RDTReceiver::get_sender_addr_key(struct sockaddr_in sender_addr) {
	// Combine IP (32 bits) and port (16 bits) into a 64-bit key
	// Upper 32 bits: IP address, Lower 16 bits: port number
	uint64_t ip = (uint64_t)sender_addr.sin_addr.s_addr;
	uint64_t port = (uint64_t)ntohs(sender_addr.sin_port);
	return (ip << 32) | port;
}

