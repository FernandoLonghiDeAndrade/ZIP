#include <iostream>
#include <string>
#include "rdt_sender.h"
#include <unistd.h>
#include <thread>
#include <cstring>
#include <vector>
#include <random>
#include <algorithm>
#include <fstream>

#define MESSAGE_COUNT 1000
#define RANDOM_SEED 12345  // SEED FIXO para gerar sempre as mesmas mensagens

// Função para gerar uma string aleatória com seed fixo
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

// Função para gerar o gabarito das mensagens (mesmo algoritmo do receiver)
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
    std::cout << "=== SENDER RDT: Enviando " << MESSAGE_COUNT << " mensagens (determinísticas) ===" << std::endl;
    std::cout << "Seed usado: " << RANDOM_SEED << std::endl;
    
    // Criar sender
    RDTSender* sender = new RDTSender();
    
    // Gerar mensagens com seed fixo
    std::vector<std::string> messages = generate_expected_messages();
    
    std::cout << "Iniciando envio de mensagens..." << std::endl;
    
    for (int i = 0; i < MESSAGE_COUNT; i++) {
        const std::string& message = messages[i];
        
        // Preparar dados para envio (array de char)
        char send_data[256] = {0};
        strncpy(send_data, message.c_str(), std::min(255, (int)message.length()));
        
        std::cout << "Enviando mensagem " << (i + 1) << "/" << MESSAGE_COUNT 
                  << " (len=" << message.length() << "): " << message << std::endl;
        
        // Enviar mensagem
        sender->send(send_data);
        
        // Pequena pausa entre envios
        //std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "\n=== ENVIO CONCLUÍDO ===" << std::endl;
    std::cout << "Total de mensagens enviadas: " << messages.size() << std::endl;
    std::cout << "Seed: " << RANDOM_SEED << " (receiver deve usar o mesmo seed)" << std::endl;
    
    // Aguardar um pouco antes de finalizar
    //std::cout << "Aguardando alguns segundos para garantir envio..." << std::endl;
    //std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Cleanup
    delete sender;
    
    std::cout << "Sender finalizado." << std::endl;
    return 0;
}