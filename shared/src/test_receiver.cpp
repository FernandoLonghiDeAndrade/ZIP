#include <iostream>
#include <string>
#include "rdt_receiver.h"
#include <unistd.h>
#include <thread>
#include <cstring>
#include <vector>
#include <random>
#include <chrono>

#define MESSAGE_COUNT 1000
#define TIMEOUT_SECONDS 30
#define RANDOM_SEED 12345  // MESMO SEED do sender

// Função para gerar uma string aleatória com seed fixo (MESMA FUNÇÃO do sender)
std::string generate_deterministic_string(int length, std::mt19937& gen) {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<> dis(0, chars.size() - 1);
    
    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; ++i) {
        result += chars[dis(gen)];
    }
    return result;
}

// Função para gerar o gabarito das mensagens (mesmo algoritmo do sender)
std::vector<std::string> generate_expected_messages() {
    std::vector<std::string> messages;
    std::mt19937 gen(RANDOM_SEED);  // Mesmo seed
    std::uniform_int_distribution<> length_dis(10, 50);
    
    for (int i = 0; i < MESSAGE_COUNT; i++) {
        int msg_length = length_dis(gen);
        std::string message = generate_deterministic_string(msg_length, gen);
        messages.push_back(message);
    }
    
    return messages;
}

int main() {
    std::cout << "=== RECEIVER RDT: Aguardando " << MESSAGE_COUNT << " mensagens ===" << std::endl;
    std::cout << "Seed usado para gabarito: " << RANDOM_SEED << std::endl;
    
    // Gerar gabarito das mensagens esperadas
    std::vector<std::string> expected_messages = generate_expected_messages();
    std::cout << "Gabarito gerado com " << expected_messages.size() << " mensagens" << std::endl;
    
    // Criar receiver
    RDTReceiver* receiver = new RDTReceiver();
    
    // Vector para armazenar as mensagens recebidas
    std::vector<std::string> received_messages;
    
    std::cout << "Receiver iniciado, aguardando mensagens..." << std::endl;
    std::cout << "Timeout: " << TIMEOUT_SECONDS << " segundos" << std::endl;
    
    char data[256];
    int received_count = 0;
    
    // Controle de timeout
    auto start_time = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::seconds(TIMEOUT_SECONDS);
    
    while (received_count < MESSAGE_COUNT) {
        // Verificar timeout
        auto current_time = std::chrono::steady_clock::now();
        if (current_time - start_time > timeout_duration) {
            std::cout << "\nTimeout atingido! Parando recepção..." << std::endl;
            break;
        }
        
        if (receiver->receive(data)) {
            std::string received_msg(data);
            received_messages.push_back(received_msg);
            received_count++;
            
            std::cout << "Mensagem " << received_count << "/" << MESSAGE_COUNT 
                      << " recebida (len=" << received_msg.length() << "): " 
                      << received_msg << std::endl;
            
            // Reset do timeout a cada mensagem recebida
            start_time = std::chrono::steady_clock::now();
        }
        
        // Pequena pausa para evitar busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "\n=== RECEPÇÃO FINALIZADA ===" << std::endl;
    std::cout << "Total de mensagens recebidas: " << received_count << std::endl;
    
    // VERIFICAÇÃO COM GABARITO INTERNO
    std::cout << "\n=== VERIFICAÇÃO DE INTEGRIDADE (COM GABARITO INTERNO) ===" << std::endl;
    
    bool test_passed = true;
    
    // Verificar se o número de mensagens está correto
    if (expected_messages.size() != received_messages.size()) {
        std::cout << "ERRO: Número de mensagens esperadas (" << expected_messages.size() 
                  << ") e recebidas (" << received_messages.size() << ") não confere!" << std::endl;
        test_passed = false;
    } else {
        std::cout << "✓ Número de mensagens correto" << std::endl;
    }
    
    // Verificar se as mensagens estão em ordem e são idênticas
    int errors = 0;
    for (size_t i = 0; i < expected_messages.size() && i < received_messages.size(); i++) {
        if (expected_messages[i] != received_messages[i]) {
            errors++;
            if (errors <= 5) { // Mostrar apenas os primeiros 5 erros
                std::cout << "ERRO na mensagem " << (i + 1) << ":" << std::endl;
                std::cout << "  Esperada: '" << expected_messages[i] << "'" << std::endl;
                std::cout << "  Recebida: '" << received_messages[i] << "'" << std::endl;
            }
            test_passed = false;
        }
    }
    
    if (errors > 5) {
        std::cout << "... e mais " << (errors - 5) << " erros não mostrados." << std::endl;
    }
    
    if (test_passed) {
        std::cout << "✓ Todas as mensagens foram recebidas em ordem e sem erros" << std::endl;
        std::cout << "\n🎉 TESTE PASSOU! O protocolo RDT está funcionando corretamente." << std::endl;
    } else {
        std::cout << "\n❌ TESTE FALHOU! Há problemas na transmissão." << std::endl;
        std::cout << "Total de erros encontrados: " << errors << std::endl;
    }
    
    // Mostrar algumas estatísticas
    std::cout << "\n=== ESTATÍSTICAS ===" << std::endl;
    std::cout << "Mensagens esperadas: " << expected_messages.size() << std::endl;
    std::cout << "Mensagens recebidas: " << received_messages.size() << std::endl;
    std::cout << "Taxa de sucesso: " << (100.0 * received_messages.size() / expected_messages.size()) << "%" << std::endl;
    
    // Cleanup
    delete receiver;
    
    return test_passed ? 0 : 1;
}