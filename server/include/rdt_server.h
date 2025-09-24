#include <netinet/in.h>
#include <unordered_map>

class RDTServer {
public:
    RDTServer(int port);
    ~RDTServer();
    Request get_request();
    std::unordered_map<uint32_t, Packet> request_map;
    void send_response(Response resp);
private:
    bool is_retransmission(Packet packet, uint32_t source_addr);
    int sockfd;
	struct sockaddr_in serv_addr;
};