#include <iostream>
#include <cstdint>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

int main() {
    std::cout << "=== POR QUE PRECISA DE 64 BITS? ===" << std::endl;
    std::cout << std::endl;
    
    // Explicar representação em bits
    std::cout << "1. REPRESENTAÇÃO EM BITS (não dígitos decimais):" << std::endl;
    std::cout << "   - Endereço IP: 32 bits (4 bytes)" << std::endl;
    std::cout << "   - Porta:       16 bits (2 bytes)" << std::endl;
    std::cout << "   - Total:       48 bits necessários" << std::endl;
    std::cout << "   - Próximo tipo maior: uint64_t (64 bits)" << std::endl;
    std::cout << std::endl;
    
    // Exemplo com IP real
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("192.168.1.100");
    addr.sin_port = htons(8080);
    
    std::cout << "2. EXEMPLO COM IP 192.168.1.100:8080:" << std::endl;
    std::cout << "   IP em decimal: " << inet_ntoa(addr.sin_addr) << std::endl;
    
    // Mostrar IP em hexadecimal
    uint32_t ip_value = ntohl(addr.sin_addr.s_addr);
    uint16_t port_value = ntohs(addr.sin_port);
    
    std::cout << "   IP em hex:     0x" << std::hex << ip_value << std::dec;
    std::cout << " (" << ip_value << " decimal)" << std::endl;
    std::cout << "   Porta em hex:  0x" << std::hex << port_value << std::dec;
    std::cout << " (" << port_value << " decimal)" << std::endl;
    std::cout << std::endl;
    
    // Mostrar em binário (simulado)
    std::cout << "3. EM BINÁRIO (representação simplificada):" << std::endl;
    std::cout << "   IP:    32 bits -> ["; 
    for(int i = 31; i >= 0; i--) {
        std::cout << ((ip_value >> i) & 1);
        if(i % 8 == 0 && i > 0) std::cout << " ";
    }
    std::cout << "]" << std::endl;
    
    std::cout << "   Porta: 16 bits -> [";
    for(int i = 15; i >= 0; i--) {
        std::cout << ((port_value >> i) & 1);
        if(i % 8 == 0 && i > 0) std::cout << " ";
    }
    std::cout << "]" << std::endl;
    std::cout << std::endl;
    
    // Mostrar combinação em 64 bits
    uint64_t combined = ((uint64_t)ip_value << 32) | port_value;
    std::cout << "4. COMBINADO EM 64 BITS:" << std::endl;
    std::cout << "   Fórmula: (IP << 32) | porta" << std::endl;
    std::cout << "   Resultado: 0x" << std::hex << combined << std::dec << std::endl;
    std::cout << "   Decimal: " << combined << std::endl;
    std::cout << std::endl;
    
    // Mostrar por que 32 bits não funciona
    std::cout << "5. POR QUE 32 BITS NÃO FUNCIONA:" << std::endl;
    std::cout << "   uint32_t máximo: " << UINT32_MAX << std::endl;
    std::cout << "   IP maior possível: 255.255.255.255 = " << 0xFFFFFFFF << std::endl;
    std::cout << "   Se tentarmos fazer (IP << 16) + porta:" << std::endl;
    std::cout << "   " << 0xFFFFFFFF << " << 16 = " << ((uint64_t)0xFFFFFFFF << 16) << std::endl;
    std::cout << "   Isso já ultrapassa 32 bits!" << std::endl;
    std::cout << std::endl;
    
    // Alternativas
    std::cout << "6. ALTERNATIVAS SEM 64 BITS:" << std::endl;
    std::cout << "   a) Usar apenas IP como chave (perde informação de porta)" << std::endl;
    std::cout << "   b) Usar string como chave: \"IP:porta\"" << std::endl;
    std::cout << "   c) Usar struct como chave" << std::endl;
    std::cout << "   d) Hash do IP+porta em 32 bits (pode ter colisões)" << std::endl;
    std::cout << std::endl;
    
    // Teste de recuperação
    std::cout << "7. TESTE DE RECUPERAÇÃO:" << std::endl;
    uint32_t recovered_ip = (uint32_t)(combined >> 32);
    uint16_t recovered_port = (uint16_t)(combined & 0xFFFF);
    
    struct sockaddr_in recovered_addr;
    recovered_addr.sin_addr.s_addr = htonl(recovered_ip);
    recovered_addr.sin_port = htons(recovered_port);
    
    std::cout << "   IP original:    " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
    std::cout << "   IP recuperado:  " << inet_ntoa(recovered_addr.sin_addr) << ":" << ntohs(recovered_addr.sin_port) << std::endl;
    std::cout << "   Match: " << (memcmp(&addr.sin_addr, &recovered_addr.sin_addr, sizeof(addr.sin_addr)) == 0 && 
                                   addr.sin_port == recovered_addr.sin_port ? "✅ SIM" : "❌ NÃO") << std::endl;
    
    return 0;
}