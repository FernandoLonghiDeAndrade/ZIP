#include <netinet/in.h>

class RDTServer {
public:
    RDTServer(int port);
    ~RDTServer();
    Request get_request();
    void send_response(Response resp);
private:
    int sockfd;
    bool expected_seq_number = 0;
	struct sockaddr_in serv_addr, cli_addr;
};