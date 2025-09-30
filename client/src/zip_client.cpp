#include "zip_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <thread>
#include <iostream>
#include <chrono>
#include <limits>
#include "print_utils.h"

ZIPClient::ZIPClient(std::string server_port, std::string server_ip) {
    // Inicializa com endereço de broadcast
    this->server_ip = 0xFFFFFFFF; // Broadcast address
    this->server_port = std::stoi(server_port);
    
    memset(&this->socket_addr, 0, sizeof(socket_addr));
    if ((this->socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        std::cerr << "ERROR opening socket" << std::endl;
        exit(1);
    } else {
        std::cout << "Socket successfully created" << std::endl;
    }

    // Habilita broadcast
    int broadcast = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        std::cerr << "ERROR enabling broadcast" << std::endl;
        exit(1);
    }
}

void ZIPClient::run() {
    this->connect_to_server();

    while (true) {
        int32_t dst_ip;
        int32_t value;
        std::cin >> dst_ip >> value;
        
        if (std::cin.fail()) {
            std::cerr << "Invalid input. Please enter two numbers." << std::endl;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }        
        this->transfer(dst_ip, value);
    }
}

void ZIPClient::connect_to_server() {
    std::cout << "Discovering server via broadcast..." << std::endl;

    // Configura endereço para broadcast
    this->socket_addr.sin_family = AF_INET;
    this->socket_addr.sin_port = htons(this->server_port);
    this->socket_addr.sin_addr.s_addr = INADDR_BROADCAST; // Modo broadcast
    bzero(&(this->socket_addr.sin_zero), 8);

    // Envia requisição de descoberta em broadcast
    Request req = Request(DISCOVERY, 0); // dst_ip não importa para discovery
    Reply discovery_reply = this->send_request_and_wait(req);
    
    // Salva o endereço real do servidor da resposta
    this->server_ip = discovery_reply.server_ip;
    this->server_port = discovery_reply.server_port;

    // Desabilita broadcast e conecta diretamente ao servidor
    int broadcast = 0;
    setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    // Reconfigura socket para comunicação direta com o servidor
    this->socket_addr.sin_family = AF_INET;
    this->socket_addr.sin_port = htons(this->server_port);
    this->socket_addr.sin_addr.s_addr = htonl(this->server_ip);
    bzero(&(this->socket_addr.sin_zero), 8);

    using namespace PrintUtils;
    print_time();
    std::cout   << " Connected to server at " << ip_to_string(this->server_ip) 
                << ":" << this->server_port << std::endl;
}

Reply ZIPClient::send_request_and_wait(const Request& req) {
    Reply reply;

    this->send_request(req);
    this->start_timer();

    while (true) {
        if (this->timeout()) {
            this->send_request(req);
            this->start_timer();
        }

        reply = this->receive_reply();
        if (reply.seq_number == this->seq_number) {
            this->seq_number++;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return reply;
}

void ZIPClient::send_request(const Request& req) {
    sendto(this->socket_fd, &req, sizeof(req), 0, (struct sockaddr*) &this->socket_addr, sizeof(struct sockaddr));
}

Reply ZIPClient::receive_reply() {
    Reply reply = Reply();

    if (recvfrom(this->socket_fd, &reply, sizeof(reply), MSG_DONTWAIT, NULL, NULL) <= 0) {
        reply.seq_number = -1;
    }
    
    return reply;
}

void ZIPClient::transfer(uint32_t dst_ip, uint32_t value) {
    Request req = Request(TRANSFER, dst_ip, this->seq_number, value);
    Reply reply = this->send_request_and_wait(req);
    
    using namespace PrintUtils;
    print_time();
    std::cout 	<< " server " << ip_to_string(this->server_ip)
      			<< " id req " << reply.seq_number
                << " dest " << ip_to_string(dst_ip)
             	<< " value " << req.value
                << " new balance " << reply.new_balance << std::endl;

    this->balance = reply.new_balance;
}

void ZIPClient::start_timer() {
    this->start_time = std::chrono::steady_clock::now();
}

bool ZIPClient::timeout() {
    using namespace std::chrono;
    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - this->start_time);
    return elapsed >= this->timeout_duration;
}