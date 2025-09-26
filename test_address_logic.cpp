#include <iostream>
#include <cstdint>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

// Test the address key logic
uint64_t get_sender_addr_key(struct sockaddr_in sender_addr) {
    uint64_t ip = (uint64_t)sender_addr.sin_addr.s_addr;
    uint64_t port = (uint64_t)ntohs(sender_addr.sin_port);
    return (ip << 32) | port;
}

struct sockaddr_in get_sender_addr(uint64_t sender_addr_key) {
    struct sockaddr_in sender_addr;
    memset(&sender_addr, 0, sizeof(sender_addr));
    sender_addr.sin_family = AF_INET;
    sender_addr.sin_addr.s_addr = (uint32_t)(sender_addr_key >> 32);
    sender_addr.sin_port = htons((uint16_t)(sender_addr_key & 0xFFFF));
    return sender_addr;
}

int main() {
    // Test with localhost:4000
    struct sockaddr_in original_addr;
    memset(&original_addr, 0, sizeof(original_addr));
    original_addr.sin_family = AF_INET;
    original_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    original_addr.sin_port = htons(4000);
    
    std::cout << "=== TESTE DA LÓGICA DE ENDEREÇAMENTO ===" << std::endl;
    std::cout << "Original IP: " << inet_ntoa(original_addr.sin_addr) << std::endl;
    std::cout << "Original Port: " << ntohs(original_addr.sin_port) << std::endl;
    
    // Convert to key
    uint64_t key = get_sender_addr_key(original_addr);
    std::cout << "Key (uint64): " << key << std::endl;
    std::cout << "Key (hex): 0x" << std::hex << key << std::dec << std::endl;
    
    // Convert back
    struct sockaddr_in recovered_addr = get_sender_addr(key);
    std::cout << "Recovered IP: " << inet_ntoa(recovered_addr.sin_addr) << std::endl;
    std::cout << "Recovered Port: " << ntohs(recovered_addr.sin_port) << std::endl;
    
    // Check if they match
    bool ip_match = (original_addr.sin_addr.s_addr == recovered_addr.sin_addr.s_addr);
    bool port_match = (original_addr.sin_port == recovered_addr.sin_port);
    
    std::cout << "\n=== RESULTADO ===" << std::endl;
    std::cout << "IP matches: " << (ip_match ? "✅ SIM" : "❌ NÃO") << std::endl;
    std::cout << "Port matches: " << (port_match ? "✅ SIM" : "❌ NÃO") << std::endl;
    std::cout << "Test " << (ip_match && port_match ? "PASSOU" : "FALHOU") << std::endl;
    
    return 0;
}