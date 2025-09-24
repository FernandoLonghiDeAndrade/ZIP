#pragma once
#include "../include/packet.h"
#include <netinet/in.h>
#include <netdb.h>
#include <string>

class RDTClient {
public:
    RDTClient(std::string server_ip);
    ~RDTClient();
    Response request(Request req);
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