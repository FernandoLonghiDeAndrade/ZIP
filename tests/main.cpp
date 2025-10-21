#include "subprocess.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <random>
#include <memory>


bool validate_balance(const std::string& line, const int expected_balance, int &found_balance) {
    // Procura por "new_balance "
    std::string key = "new_balance ";
    found_balance = -1;
    auto pos = line.find(key);
    if (pos == std::string::npos) return true; // linha não contém saldo, ignora
    pos += key.size();
    // Extrai o número após "new_balance "
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
        // Decide program names depending on platform
#ifdef _WIN32
        const std::string server_prog = "server.exe";
        const std::string client_prog = "client.exe";
#else
        const std::string server_prog = "./server";
        const std::string client_prog = "./client";
#endif

        std::vector<std::string> client_ips = {"192.168.1.156", "192.168.1.156", "192.168.1.156"};
        std::string server_ip_port = "8080";

        const long INITIAL_BALANCE = 100;

        // Start server process with argument 8080
        proc::Subprocess server_proc;
        proc::StartInfo server_si;
        server_si.program = server_prog;
        server_si.args = {server_ip_port};
        std::cout << "Starting server: " << server_si.program << " " << server_ip_port << "\n";
        server_proc.start(server_si);

        // Give server a moment to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Launch 3 clients in separate threads, each starts a client process with arg 8080
        const int NUM_CLIENTS = client_ips.size();
        std::vector<std::thread> threads;

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

        for (int i = 0; i < NUM_CLIENTS; ++i) {
            threads.emplace_back([i, &client_procs, &client_ips, &client_balances, NUM_CLIENTS]() {
                try {
                    proc::Subprocess &client = *client_procs[i];
                    std::string line;
                    // Random generator per client thread
                    std::mt19937 rng(static_cast<unsigned>(std::random_device{}()) + i);
                    std::uniform_int_distribution<int> balance_sent(100, 1000);
                    std::uniform_int_distribution<int> client_choose(0, NUM_CLIENTS - 1);

                    long client_balance = INITIAL_BALANCE;

                    for(int count = 0; count <= 100; ++count) {
                        int money_sent = balance_sent(rng);
                        int target_client = client_choose(rng);
                        client_balance -= money_sent;
                        client_balances[i] -= money_sent;
                        client_balances[i] += money_sent;
                        std::string cmd = std::string(client_ips[i]) + " " + std::to_string(money_sent) + "\n";
                        
                        client.write_stdin(cmd.data(), cmd.size());
                        
                        //std::cout << count << " [client " << i << " - " << client_ips[i] << "] send to " << "[client " << target_client << " - " << client_ips[target_client] << "]: " << money_sent << "\n";

                        line.clear();
                        size_t pos = line.find("new_balance");

                        while (line.find("new_balance") == std::string::npos) {
                            client.read_stdout_line(line);
                            int found_balance = 0;
                            if (!validate_balance(line, client_balances[i], found_balance)) {
                                //std::cerr << count << " Balance validation failed for client " << i << "\n";
                            } else if (found_balance != -1) {
                                //std::cout << count << " [client " << i << " - " << client_ips[i] << "] New balance: " << found_balance << " OK!\n";
                                break;
                            }
                        }
                        // short sleep between commands
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                } catch (const std::system_error& e) {
                    std::cerr << "client " << i << " error: " << e.code() << " - " << e.what() << "\n";
                }
            });
        }

        // Wait for all client threads to finish
        for (auto &t : threads) {
            t.detach();
            t.join();
        }
        // Finaliza os processos dos clientes
        for (auto& proc_ptr : client_procs) {
            if (proc_ptr) {
                try {
                    proc_ptr->terminate(); // envia sinal de término
                    proc_ptr->wait();      // espera encerrar
                } catch (...) {
                    std::cerr << "Failed to terminate client process.\n";
                }
            }
        }

        // Finaliza o processo do servidor
        try {
            server_proc.terminate();
            server_proc.wait();
        } catch (...) {
            std::cerr << "Failed to terminate server process.\n";
        }

    } catch (const std::system_error& e) {
        std::cerr << "erro: " << e.code() << " - " << e.what() << "\n";
        return 1;
    }
    return 0;
}
