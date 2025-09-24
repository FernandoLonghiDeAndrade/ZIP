
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
#include "rdt_client.h"

RDTClient::RDTClient(std::string server_ip, int port) {

	server = gethostbyname(server_ip.c_str());
	if (server == NULL) {
        fprintf(stderr,"RDT SENDER ERROR, host not found\n");
        exit(0);
    }		
	if ((this->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		printf("ERROR opening socket");
	
	serv_addr.sin_family = AF_INET;     
	serv_addr.sin_port = htons(port);    
	serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
	bzero(&(serv_addr.sin_zero), 8);  

}

Response RDTClient::request(Request req) {

	Packet request_packet = Packet(this->seq_number, req);
	Packet response_packet = Packet(-1, Response());
	Response resp = Response();

	bool timeout = true;

	this->send_packet(request_packet);
	auto start = std::chrono::high_resolution_clock::now();

	int32_t client_ip = inet_addr("127.0.0.1");

	while (true) {

		// Receive ack
		response_packet = this->receive_packet();
		if (this->is_ack(response_packet)) {
			this->seq_number++;
			resp = response_packet.data.resp;
			break;
		}

		// Check timeout
		auto now = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
		if (elapsed > this->timeout_seconds) {
			std::cout << "Timeout, resending packet..." << std::endl;
			this->send_packet(request_packet);
			start = std::chrono::high_resolution_clock::now();
		}

		// Sleep for a short duration to avoid busy waiting
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return resp;
}

void RDTClient::send_packet(Packet packet) {
	int n = sendto(this->sockfd, &packet, sizeof(packet), 0, (struct sockaddr *) &this->serv_addr, sizeof(struct sockaddr));
	if (n < 0) 
		printf("ERROR sending packet");
}

Packet RDTClient::receive_packet() {
	Packet packet = Packet(-1, Response());
	socklen_t fromlen = sizeof(this->from);
	int n = recvfrom(this->sockfd, &packet, sizeof(packet), 0, (struct sockaddr *) &this->from, &fromlen);
	if (n < 0) 
		printf("ERROR receiving packet");
	return packet;
}

bool RDTClient::is_ack(Packet ack_packet) {
	bool seq_number_matches = ack_packet.seq_number == this->seq_number;
	bool data_is_ack = ack_packet.type == REQ_ACK;
	return seq_number_matches && data_is_ack;
}

RDTClient::~RDTClient() {
    close(sockfd);
}