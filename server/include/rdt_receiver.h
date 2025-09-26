#include <netinet/in.h>
#include <unordered_map>
#include <cstdint>
#include "request.h"
#include "rdt_mail.h"

class RDTReceiver {
public:
    RDTReceiver(int port);
    ~RDTReceiver();
    RDTMail get_mail();
    std::unordered_map<uint64_t, Packet> request_map;
    void send_response(Response resp, uint64_t dest_addr);
private:
    bool is_retransmission(Packet packet, uint64_t source_addr);
    struct sockaddr_in get_sender_addr(uint64_t sender_addr_key);
    uint64_t get_sender_addr_key(struct sockaddr_in sender_addr);
    int sockfd;
	struct sockaddr_in serv_addr;
    std::mutex mutex;
};



