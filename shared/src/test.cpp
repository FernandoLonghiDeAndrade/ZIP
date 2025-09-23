#include <iostream>
#include <string>
#include "../include/rdt_receiver.h"
#include "../include/rdt_sender.h"
#include <unistd.h>

int main() {
    RDTReceiver receiver = RDTReceiver();
    RDTSender sender = RDTSender();

    for (int i = 0; i < 5; i++) {
        std::string message = "Message " + std::to_string(i);
        sender.send((char*)message.c_str());
        sleep(1); // Espera un segundo entre envíos
    }
    return 0;
}