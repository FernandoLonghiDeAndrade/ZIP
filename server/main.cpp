#include <iostream>
#include "zip_server.h"

int main(int argc, char *argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " <server_port>" << std::endl;
        return 1;
    }

    ZIPServer server = (argc == 2) ? ZIPServer(argv[1]) : ZIPServer();

    server.run();

    return 0;
}
