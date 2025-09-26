#include <iostream>
#include <limits>
#include <arpa/inet.h>
#include "client.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server_address>" << std::endl;
        return 1;
    }
    
    std::string server_address = argv[1];
    Client client = Client();
        
    // Example usage - you can extend this
    // client.connect(server_address);
    // client.send_request(...);

    while (true) {
        int32_t dest_addr;
        int32_t value;
        std::cin >> dest_addr >> value;
        
        if (std::cin.fail()) {
            std::cerr << "Invalid input. Please enter two numbers." << std::endl;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }        
        int32_t new_balance = client.transfer(dest_addr, value);
        if (new_balance != -1) {
            
        } else {
            std::cout << "Transfer failed." << std::endl;
        }
    }
    
    return 0;
}