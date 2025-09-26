#include "request.h"
#include "response.h"
#include <netinet/in.h>
#include <cstdint>

struct RDTMail {
    uint64_t sender_addr;
    Request req;
};