#pragma once
#include "../include/packet.h"
#include "request.h"
#include "response.h"
#include <netinet/in.h>
#include <netdb.h>
#include <string>

class RDTSender {
public:
    RDTSender(std::string server_ip, int port);
    ~RDTSender();
    Response request(Request req);
    void set_port(int port) { this->serv_addr.sin_port = htons(port); }
private:
    int timeout_seconds = 2;
    bool is_ack(Packet response_packet);
    void send_packet(Packet packet);
    Packet receive_packet();
    int sockfd;
    int seq_number = 0;
    struct hostent *server;
    struct sockaddr_in serv_addr, from;
};

RDTSender sender = RDTSender("localhost", 4000);