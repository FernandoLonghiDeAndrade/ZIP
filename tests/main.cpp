#include "subprocess.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <random>
#include <memory>

int main() {
    try {
        // Decide program names depending on platform
#ifdef _WIN32
        const std::string server_prog = "server.exe";
        const std::string client_prog = "client.exe";
#else
        const std::string server_prog = "./server";
        const std::string client_prog = "./client";
#endif

        // Start server process with argument 8080
        proc::Subprocess server_proc;
        proc::StartInfo server_si;
        server_si.program = server_prog;
        server_si.args = {"8080"};
        std::cout << "Starting server: " << server_si.program << " 8080\n";
        server_proc.start(server_si);

        // Give server a moment to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Launch 3 clients in separate threads, each starts a client process with arg 8080
        const int NUM_CLIENTS = 3;
        std::vector<std::thread> threads;
        std::mutex cout_m;

        // Create persistent client processes, one per thread, keep stdin open and send commands in a loop
        std::vector<std::unique_ptr<proc::Subprocess>> client_procs(NUM_CLIENTS);
        std::vector<proc::StartInfo> cis(NUM_CLIENTS);
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            cis[i].program = client_prog;
            cis[i].args = {"8080"};
            client_procs[i] = std::make_unique<proc::Subprocess>();
            client_procs[i]->start(cis[i]);
        }

        for (int i = 0; i < NUM_CLIENTS; ++i) {
            threads.emplace_back([i, &client_procs, &cout_m]() {
                try {
                    proc::Subprocess &client = *client_procs[i];
                    std::string line;
                    // Random generator per client thread
                    std::mt19937 rng(static_cast<unsigned>(std::random_device{}()) + i);
                    std::uniform_int_distribution<int> dist(100, 1000);

                    while (true) {
                        int x = dist(rng);
                        std::string cmd = std::string("192.168.1.156 ") + std::to_string(x) + "\n";
                        client.write_stdin(cmd.data(), cmd.size());
                        // read any available output lines from client (non-blocking read loop would be better, but use blocking read here)
                        if (client.read_stdout_line(line)) {
                            std::lock_guard<std::mutex> lk(cout_m);
                            std::cout << "[client " << i << "] " << line;
                        }
                        // short sleep between commands
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    }
                } catch (const std::system_error& e) {
                    std::lock_guard<std::mutex> lk(cout_m);
                    std::cerr << "client " << i << " error: " << e.code() << " - " << e.what() << "\n";
                }
            });
        }

        // Wait for all client threads to finish
        for (auto &t : threads) t.join();

        std::cout << "All clients finished. Server may still be running.\n";
        std::cout << "If you want the test to stop the server automatically, modify this test to signal/terminate the server.\n";

        // Optionally wait for server to exit if it does on its own
        // Here we try a non-blocking wait: if wait() blocks, user can uncomment the following line
        // int server_ec = server_proc.wait();
        // std::cout << "Server exit code: " << server_ec << "\n";

    } catch (const std::system_error& e) {
        std::cerr << "erro: " << e.code() << " - " << e.what() << "\n";
        return 1;
    }
    return 0;
}
