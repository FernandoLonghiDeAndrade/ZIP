#include "subprocess.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <random>
#include <memory>
#include <atomic>

// Checks if the "new_balance" field in the client response matches the expected value
bool validate_balance(const std::string& line, const int expected_balance, int &found_balance) {
    // Look for "new_balance "
    std::string key = "new_balance ";
    found_balance = -1;
    auto pos = line.find(key);
    if (pos == std::string::npos) return true; // ignore lines without balance
    pos += key.size();
    // Extract the number after "new_balance "
    size_t end = line.find_first_not_of("0123456789", pos);
    std::string num_str = (end == std::string::npos) ? line.substr(pos) : line.substr(pos, end - pos);
    if (num_str.empty()) return false;
    try {
        found_balance = std::stoi(num_str);
    } catch (...) {
        return false;
    }
    return found_balance == expected_balance;
}

int main() {
    try {
        // Select program names depending on platform (Windows or Unix)
#ifdef _WIN32
        const std::string server_prog = "server.exe";
        const std::string client_prog = "client.exe";
#else
        const std::string server_prog = "./server";
        const std::string client_prog = "./client";
#endif

        // List of client IPs (all the same here, but could be different)
        std::vector<std::string> client_ips = {"192.168.1.156", "192.168.1.156", "192.168.1.156"};
        std::string server_ip_port = "8080";

        const long INITIAL_BALANCE = 100;
        const int TEST_COUNT = 100;

        // Summary variables for test results
        std::atomic<int> total_tests{0};
        std::atomic<int> success_tests{0};
        std::atomic<int> failed_tests{0};
        std::atomic<int> timeout_tests{0};

        // Mutex for cout/cerr to avoid interleaved output
        std::mutex cout_mutex;

        // Start the server process with argument 8080
        proc::Subprocess server_proc;
        proc::StartInfo server_si;
        server_si.program = server_prog;
        server_si.args = {server_ip_port};
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Starting server: " << server_si.program << " " << server_ip_port << "\n";
        }
        server_proc.start(server_si);

        // Give the server a moment to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Number of clients
        const int NUM_CLIENTS = client_ips.size();
        std::vector<std::thread> threads;

        // Track each client's balance
        std::vector<long> client_balances(NUM_CLIENTS, INITIAL_BALANCE);

        // Create persistent client processes, one per thread, keep stdin open and send commands in a loop
        std::vector<std::unique_ptr<proc::Subprocess>> client_procs(NUM_CLIENTS);
        std::vector<proc::StartInfo> cis(NUM_CLIENTS);
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            cis[i].program = client_prog;
            cis[i].args = {server_ip_port};
            client_procs[i] = std::make_unique<proc::Subprocess>();
            client_procs[i]->start(cis[i]);
        }

        // Launch a thread for each client process
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            threads.emplace_back([i, &client_procs, &client_ips, &client_balances, NUM_CLIENTS, TEST_COUNT,
                                  &total_tests, &success_tests, &failed_tests, &timeout_tests, &cout_mutex]() {
                try {
                    proc::Subprocess &client = *client_procs[i];
                    std::string line;
                    // Random generator per client thread
                    std::mt19937 rng(static_cast<unsigned>(std::random_device{}()) + i);
                    std::uniform_int_distribution<int> balance_sent(100, 1000);
                    std::uniform_int_distribution<int> client_choose(0, NUM_CLIENTS - 1);

                    long client_balance = INITIAL_BALANCE;

                    // Each client sends 100 transactions
                    for(int count = 0; count < TEST_COUNT; ++count) {
                        total_tests.fetch_add(1);

                        int money_sent = balance_sent(rng);
                        int target_client = client_choose(rng);

                        // Update local balance (for validation)
                        client_balance -= money_sent;
                        client_balances[i] -= money_sent;
                        client_balances[i] += money_sent;

                        // Prepare and send command to client process
                        std::string cmd = std::string(client_ips[i]) + " " + std::to_string(money_sent) + "\n";
                        client.write_stdin(cmd.data(), cmd.size());

                        {
                            std::lock_guard<std::mutex> lock(cout_mutex);
                            std::cout << count+1 << "/" << TEST_COUNT << " [client " << i << " - " << client_ips[i] << "] send to "
                                      << "[client " << target_client << " - " << client_ips[target_client] << "]: "
                                      << money_sent << "\n";
                        }

                        // Wait up to 100ms for a response line from the client
                        line.clear();
                        int found_balance = 0;
                        bool got_balance = false;
                        auto start = std::chrono::steady_clock::now();
                        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
                            if (client.read_stdout_line(line)) {
                                if(!line.empty()) {
                                    std::lock_guard<std::mutex> lock(cout_mutex);
                                    std::cout << count+1 << "/" << TEST_COUNT << " [client " << i << "] Response: " << line;
                                }
                                if (!validate_balance(line, client_balances[i], found_balance)) {
                                    std::lock_guard<std::mutex> lock(cout_mutex);
                                    std::cerr << count+1 << "/" << TEST_COUNT << " Balance validation failed for client " << i << "\n";
                                    failed_tests.fetch_add(1);
                                    got_balance = true;
                                    break;
                                } else if (found_balance != -1) {
                                    std::lock_guard<std::mutex> lock(cout_mutex);
                                    std::cout << count+1 << "/" << TEST_COUNT << " [client " << i << " - " << client_ips[i]
                                            << "] New balance: " << found_balance << " OK!\n";
                                    success_tests.fetch_add(1);
                                    got_balance = true;
                                    break;
                                }
                            } else {
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            }
                        }
                        if (!got_balance) {
                            std::lock_guard<std::mutex> lock(cout_mutex);
                            std::cerr << count+1 << "/" << TEST_COUNT << " Timeout waiting for balance update for client " << i << "\n";
                            timeout_tests.fetch_add(1);
                        }
                        // Short sleep between commands
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                } catch (const std::system_error& e) {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cerr << "client " << i << " error: " << e.code() << " - " << e.what() << "\n";
                }
            });
        }

        // Wait for all client threads to finish
        for (auto &t : threads) t.join();

        // Terminate all client processes
        for (auto& proc_ptr : client_procs) {
            if (proc_ptr) {
                try {
                    proc_ptr->terminate(); // send termination signal
                    proc_ptr->wait();      // wait for process to exit
                } catch (...) {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cerr << "Failed to terminate client process.\n";
                }
            }
        }

        // Terminate the server process
        try {
            server_proc.terminate();
            server_proc.wait();
        } catch (...) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cerr << "Failed to terminate server process.\n";
        }

        // Print test summary
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n=== TEST SUMMARY ===\n";
            std::cout << "Total tests:      " << total_tests.load() << "\n";
            std::cout << "Success:          " << success_tests.load() << "\n";
            std::cout << "Failed:           " << failed_tests.load() << "\n";
            std::cout << "Timeout:          " << timeout_tests.load() << "\n";
            std::cout << "====================\n";
        }

    } catch (const std::system_error& e) {
        std::cerr << "erro: " << e.code() << " - " << e.what() << "\n";
        return 1;
    }
    return 0;
}
