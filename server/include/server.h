#include <netinet/in.h>
#include <unordered_map>
#include <cstdint>
#include <mutex>
#include <string>
#include "request.h"
#include "reply.h"

class Server {
public:
    Server(int16_t port);   
    ~Server();
    void run();
private:
    Request wait_for_new_request();
    Request receive_request();
    void process(Request req);
    Reply gen_discovery_reply(Request req);
    void send_reply(Reply reply, int32_t client_addr, int16_t client_port);
    bool is_duplicate(Request req);
    void print_time();
    void print_request(Request req);
    void print_server_state();
    struct sockaddr_in gen_addr(int16_t port);

    std::unordered_map<uint32_t, Reply> reply_map;
    struct sockaddr_in serv_addr;
    int sockfd;
    std::mutex mutex;

    int total_transactions = 0;
    int total_transferred = 0;
    int total_balance = 0; // Initial balance
};



