#include "packet.h"
#include <string.h>

Packet::Packet(bool seq_number, char data[256]) {
    memcpy(this->data, data, 256);
    this->seq_number = seq_number;
}