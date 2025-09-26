#include "request.h"
#include "unistd.h"

class Server {
public:
    Server(int port);
    ~Server();
    void pool_requests();
    void process_request(Request req, uint64_t sender_addr);
};