#include <cstdint>
#define DISCOVERY_PORT 4000 // Set this to the correct discovery port number

enum class RequestType : int32_t {
    DISCOVERY = 0,
    TRANSFER = 1
};