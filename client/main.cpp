#include <iostream>
#include "zip_client.h"

int main(int argc, char *argv[]) {
    if (argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <server_port> <server_ip>" << std::endl;
        return 1;
    }

    ZIPClient client = (argc == 3) ? ZIPClient(argv[1], argv[2]) : (argc == 2) ? ZIPClient(argv[1]) : ZIPClient();

    client.run();
    
    return 0;
}
