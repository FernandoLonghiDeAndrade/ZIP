
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
	Packet packet(NULL, Request());
	socklen_t clilen = sizeof(this->cli_addr);
	int n = -1;
	while (n < 0) {
		n = recvfrom(this->sockfd, &packet, sizeof(packet), 0, (struct sockaddr *) &this->cli_addr, &clilen);
	}
	return packet.data.req;
}

void RDTServer::send_response(Response resp) {
	Packet packet(resp.seq_number, resp);
	socklen_t clilen = sizeof(this->cli_addr);
	int n = sendto(this->sockfd, &packet, sizeof(packet), 0, (struct sockaddr *) &this->cli_addr, clilen);
	if (n < 0) 
		printf("ERROR sending response\n");
}
