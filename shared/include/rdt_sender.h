#pragma once
#include "../include/packet.h"

class RDTSender {
public:
    RDTSender();
    ~RDTSender();
    void send(char data[]);
private:
    bool is_valid_ack(Packet ack_packet);
    int sockfd;
    bool seq_number = 0;
    struct hostent *server;
    struct sockaddr_in serv_addr, from;
};