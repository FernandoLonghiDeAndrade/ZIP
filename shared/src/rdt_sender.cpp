
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "../include/rdt_sender.h"

#define PORT 4000

RDTSender::RDTSender() {	

	server = gethostbyname("localhost");
	if (server == NULL) {
        fprintf(stderr,"RDT SENDER ERROR, host not found\n");
        exit(0);
    }	
	
	if ((this->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		printf("ERROR opening socket");
	
	serv_addr.sin_family = AF_INET;     
	serv_addr.sin_port = htons(PORT);    
	serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
	bzero(&(serv_addr.sin_zero), 8);  

};

void RDTSender::send(char data[]) {

	bzero(this->buffer, 256);
	memcpy(this->buffer, data, strlen(data));

	int n = sendto(this->sockfd, this->buffer, strlen(this->buffer), 0, (const struct sockaddr *) &this->serv_addr, sizeof(struct sockaddr_in));
	if (n < 0) 
		printf("ERROR sendto");
	
	unsigned int length = sizeof(struct sockaddr_in);
	n = recvfrom(sockfd, buffer, 256, 0, (struct sockaddr *) &from, &length);
	if (n < 0)
		printf("ERROR recvfrom");

	printf("Got an ack: %s\n", buffer);
};

RDTSender::~RDTSender() {
    close(sockfd);
};