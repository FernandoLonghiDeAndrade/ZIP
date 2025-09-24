
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
#include "rdt_sender.h"

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

}

void RDTSender::send(char data[]) {

	Packet send_packet = Packet(this->seq_number, data);
	Packet ack_packet = Packet(-1, (char *)"");

	bool timeout = true;
	bool invalid_ack = true;

	while (timeout || invalid_ack) {

		sendto(this->sockfd, &send_packet, sizeof(send_packet), 0, (const struct sockaddr *) &this->serv_addr, sizeof(struct sockaddr_in));
		auto start = std::chrono::high_resolution_clock::now();

		while (1) {

			// Receive ack
			socklen_t fromlen = sizeof(this->from);
			recvfrom(this->sockfd, &ack_packet, sizeof(ack_packet), MSG_DONTWAIT, (struct sockaddr *) &this->from, &fromlen);
			if (is_valid_ack(ack_packet)) {
				invalid_ack = false;
				timeout = false;
				this->seq_number = (this->seq_number == 0) ? 1 : 0;
				break;
			}

			// Check timeout
			auto now = std::chrono::high_resolution_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
			if (elapsed > 1) {
				timeout = true;
				std::cout << "Timeout, resending packet..." << std::endl;
				break;
			}
		}
 
	}
}

bool RDTSender::is_valid_ack(Packet ack_packet) {
	bool seq_number_matches = ack_packet.seq_number == this->seq_number;
	bool data_is_ack = strcmp(ack_packet.data, "ACK") == 0;
	return seq_number_matches && data_is_ack;
}

RDTSender::~RDTSender() {
    close(sockfd);
}