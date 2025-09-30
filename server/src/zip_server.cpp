#include "zip_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <thread>
#include <iostream>
#include <ifaddrs.h>
#include "print_utils.h"

ZIPServer::ZIPServer(std::string server_port) {
    memset(&this->socket_addr, 0, sizeof(socket_addr));
    if ((this->socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        std::cerr << "ERROR opening socket" << std::endl;
        exit(1);
    } else {
        std::cout << "Socket successfully created" << std::endl;
    }
    
    int reuse = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "ERROR setting SO_REUSEADDR" << std::endl;
        exit(1);
    }

    // Habilita recebimento de broadcasts
    int broadcast = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        std::cerr << "ERROR enabling broadcast reception" << std::endl;
        exit(1);
    }
    
    this->socket_addr = this->gen_addr(std::stoi(server_port));
    
    if (bind(socket_fd, (struct sockaddr *) &this->socket_addr, sizeof(struct sockaddr)) < 0) {
        std::cout << "ERROR on binding" << std::endl;
        exit(1);
    } else {
        std::cout << "Socket successfully binded and listening for broadcasts" << std::endl;
    }

    PrintUtils::print_time();
    PrintUtils::print_server_state(this->total_transactions, this->total_transferred, this->total_balance);
}

void ZIPServer::run() {
    while (true) {
        Request req = wait_for_new_request();
        std::thread t(&ZIPServer::process_request, this, std::ref(req));
        t.detach();
    }
}

void ZIPServer::process_request(const Request& req) {
    Reply reply;

    if (req.type == RequestType::DISCOVERY) {
        reply = this->gen_discovery_reply(req);
    } else if (req.type == RequestType::TRANSFER) {
        reply = this->gen_discovery_reply(req);
        reply.new_balance = 100;
        
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->total_transactions++;
            this->total_transferred += req.value;
            this->total_balance -= req.value;
            PrintUtils::print_request(req, false);
            PrintUtils::print_server_state(this->total_transactions, this->total_transferred, this->total_balance);
        }
    } else {
        std::cerr << "ERROR unknown request type" << std::endl;
        return;
    }
    
    this->send_reply(reply, req.src_ip, req.src_port);
    
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->transactions.push_back({req.src_ip, req.seq_number, req.dst_ip, req.value});
        this->client_map[req.src_ip] = {req.seq_number, reply.new_balance};
    }
}

Request ZIPServer::wait_for_new_request() {
    while (true) {
        Request req = this->receive_request(); 
        ClientInfo client_info;
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            client_info = this->client_map[req.src_ip];
        }

        bool is_duplicate = req.seq_number <= client_info.seq_number;
        if (is_duplicate) {
            Reply reply(ntohl(this->socket_addr.sin_addr.s_addr), ntohs(this->socket_addr.sin_port), req.seq_number, client_info.balance);
            this->send_reply(reply, req.src_ip, req.src_port);
            PrintUtils::print_request(req, is_duplicate);
            PrintUtils::print_server_state(this->total_transactions, this->total_transferred, this->total_balance);
        } else {
            return req;
        }
    }
}

Request ZIPServer::receive_request() {
    Request req;
    struct sockaddr_in sender_addr; 
    socklen_t sender_addr_len = sizeof(sender_addr);
    recvfrom(this->socket_fd, &req, sizeof(req), 0, (struct sockaddr *) &sender_addr, &sender_addr_len);
    req.src_ip = ntohl(sender_addr.sin_addr.s_addr);
    req.src_port = ntohs(sender_addr.sin_port);
    return req;
}

void ZIPServer::send_reply(const Reply& reply, int32_t client_ip, int16_t client_port) {
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(client_ip);
    client_addr.sin_port = htons(client_port);
    bzero(&(client_addr.sin_zero), 8);
    sendto(this->socket_fd, &reply, sizeof(reply), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
}

struct sockaddr_in ZIPServer::gen_addr(int16_t port) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    bzero(&(addr.sin_zero), 8);
    return addr;
}

Reply ZIPServer::gen_discovery_reply(const Request& req) {
    Reply reply = Reply();
    // Retorna o IP real do servidor (não INADDR_ANY)
    reply.server_ip = this->get_local_ip();
    reply.server_port = ntohs(this->socket_addr.sin_port);
    reply.seq_number = req.seq_number;
    reply.new_balance = INITIAL_BALANCE;
    
    std::cout << "Sending discovery reply with IP: " << PrintUtils::ip_to_string(reply.server_ip) 
              << " Port: " << reply.server_port << std::endl;
    
    return reply;
}

uint32_t ZIPServer::get_local_ip() {
    struct ifaddrs *ifaddrs_ptr;
    uint32_t local_ip = 0;
    
    if (getifaddrs(&ifaddrs_ptr) == 0) {
        struct ifaddrs *ifa = ifaddrs_ptr;
        while (ifa != nullptr) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in* addr_in = (struct sockaddr_in*)ifa->ifa_addr;
                uint32_t ip = ntohl(addr_in->sin_addr.s_addr);
                
                // Debug: imprimir IPs encontrados
                std::cout << "Found interface: " << ifa->ifa_name 
                          << " IP: " << PrintUtils::ip_to_string(ip) << std::endl;
                
                // Procura por IP não-loopback e não 0.0.0.0
                if (ip != 0 && ip != 0x7F000001 && (ip & 0xFF000000) != 0x7F000000) {
                    // Prioriza IPs de redes privadas comuns
                    if ((ip & 0xFF000000) == 0xC0A80000 ||  // 192.168.x.x
                        (ip & 0xFFF00000) == 0xAC100000 ||  // 172.16-31.x.x
                        (ip & 0xFF000000) == 0x0A000000) {  // 10.x.x.x
                        local_ip = ip;
                        std::cout << "Selected IP: " << PrintUtils::ip_to_string(ip) << std::endl;
                        break;
                    } else if (local_ip == 0) {
                        // Se não encontrar IP privado, usa qualquer IP público válido
                        local_ip = ip;
                    }
                }
            }
            ifa = ifa->ifa_next;
        }
        freeifaddrs(ifaddrs_ptr);
    }
    
    // Se ainda não encontrou, tenta uma abordagem alternativa
    if (local_ip == 0) {
        std::cout << "Warning: Could not find local IP, using alternative method..." << std::endl;
        
        // Cria socket temporário para descobrir IP local
        int temp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (temp_sock != -1) {
            struct sockaddr_in temp_addr;
            temp_addr.sin_family = AF_INET;
            temp_addr.sin_port = htons(80);
            inet_pton(AF_INET, "8.8.8.8", &temp_addr.sin_addr); // Google DNS
            
            if (connect(temp_sock, (struct sockaddr*)&temp_addr, sizeof(temp_addr)) == 0) {
                struct sockaddr_in local_addr;
                socklen_t addr_len = sizeof(local_addr);
                if (getsockname(temp_sock, (struct sockaddr*)&local_addr, &addr_len) == 0) {
                    local_ip = ntohl(local_addr.sin_addr.s_addr);
                    std::cout << "Alternative method found IP: " << PrintUtils::ip_to_string(local_ip) << std::endl;
                }
            }
            close(temp_sock);
        }
    }
    
    if (local_ip == 0) {
        std::cerr << "ERROR: Could not determine local IP address" << std::endl;
        // Como fallback, retorna localhost
        local_ip = 0x7F000001; // 127.0.0.1
    }
    
    return local_ip;
}
