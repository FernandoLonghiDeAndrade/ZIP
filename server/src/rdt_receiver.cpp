
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
#include "rdt_receiver.h"
#include "packet.h"

#define PORT 4000

RDTReceiver::RDTReceiver() {
    if ((this->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) 
    printf("ERROR opening socket");

    printf("passou pela criacao do socket\n");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(serv_addr.sin_zero), 8);
	
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr)) < 0)
		printf("ERROR on binding");
	
	printf("passou pelo bind\n");

}

RDTReceiver::~RDTReceiver() {
    close(sockfd);
}

bool RDTReceiver::receive(char data[]) {

	Packet packet(-1, (char *)"");
	Packet ack_packet(-1, (char *)"ACK");
	
	socklen_t clilen = sizeof(this->cli_addr);
	
	int n = recvfrom(this->sockfd, &packet, sizeof(packet), 0, (struct sockaddr *) &this->cli_addr, &clilen);
	
	if (n > 0) {
		ack_packet.seq_number = packet.seq_number;
		if (packet.seq_number == this->expected_seq_number) {
			memcpy(data, packet.data, 256);
			sendto(this->sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *) &this->cli_addr, sizeof(struct sockaddr_in));
			this->expected_seq_number = (this->expected_seq_number == 0) ? 1 : 0;
			return true;
		} else {
			sendto(this->sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *) &this->cli_addr, sizeof(struct sockaddr_in));
			return false;
		}
	}

	return false;

}
