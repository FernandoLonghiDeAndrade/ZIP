
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
#include "rdt_server.h"
#include "packet.h"

RDTServer::RDTServer(int port) {
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

RDTServer::~RDTServer() {
    close(sockfd);
}

Request RDTServer::get_request() {

	Packet request_packet(NULL, Request());
	Request req = Request();
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	int n = -1;

	while (true) {

		// Read a packet from the socket
		n = recvfrom(this->sockfd, &request_packet, sizeof(request_packet), 0, (struct sockaddr *) &client_addr, &client_addr_len);
		int32_t client_ip = client_addr.sin_addr.s_addr;
		// If the packet is a retransmission, resend the corresponding response
		if (n > 0) {
			if (is_retransmission(request_packet, client_ip)) {
				send_response(this->request_map[client_ip].data.resp);
			} else {
				// Otherwise, store the packet in the retransmission map and return the request to the application
				this->request_map[client_ip] = request_packet;
				return request_packet.data.req;
			}
		}
	}

	return req;

}

bool RDTServer::is_retransmission(Packet packet, in_addr_t source_addr) {
	if (this->request_map.find(source_addr) == this->request_map.end())
		return false;
	else 
		return (this->request_map[source_addr].seq_number == packet.seq_number);
}

void RDTServer::send_response(Response resp, int32_t dest_addr) {
	int seq_number = this->request_map[dest_addr].seq_number;
	Packet packet(seq_number, resp);
	sendto(this->sockfd, &packet, sizeof(packet), 0, (struct sockaddr *) &dest_addr, sizeof(dest_addr));
}
