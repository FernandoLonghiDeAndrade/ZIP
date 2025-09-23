
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include <pthread.h>
#include "../include/rdt_receiver.h"

#define PORT 4000

RDTReceiver::RDTReceiver() {
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) 
    printf("ERROR opening socket");

    printf("passou pela criacao do socket\n");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(serv_addr.sin_zero), 8);
	
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr)) < 0)
		printf("ERROR on binding");
	
	socklen_t clilen = sizeof(struct sockaddr_in);

	printf("passou pelo bind\n");

    pthread_t thread_id;
    pthread_create(&(thread_id),NULL,RDTReceiver::look_for_data, this);
};

void* RDTReceiver::look_for_data(void* args) {

    RDTReceiver* receiver = static_cast<RDTReceiver*>(args);
    socklen_t clilen;

    while (1) {
		bzero(receiver->buffer, 256);
		clilen = sizeof(receiver->cli_addr);
		int n = recvfrom(receiver->sockfd, receiver->buffer, 256, 0, (struct sockaddr *) &receiver->cli_addr, &clilen);
		if (n < 0) 
			printf("ERROR on recvfrom");
		printf("Received a datagram: %s\n", receiver->buffer);
		n = sendto(receiver->sockfd, "Got your message\n", 17, 0,(struct sockaddr *) &receiver->cli_addr, sizeof(struct sockaddr));
		if (n  < 0)
			printf("ERROR on sendto");
	}
};

RDTReceiver::~RDTReceiver() {
    close(sockfd);
};