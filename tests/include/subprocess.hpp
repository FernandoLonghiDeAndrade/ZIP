#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <system_error>

namespace proc {

struct StartInfo {
    std::string program;              // caminho do executável
    std::vector<std::string> args;    // argv[0] NÃO é incluído aqui (será o program)
    std::string working_dir;          // "" => atual
    bool redirect_stderr_to_stdout = false;
};

class Subprocess {
public:
    Subprocess();
    ~Subprocess();

    // Não copiável; movível
    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;
    Subprocess(Subprocess&& other) noexcept;
    Subprocess& operator=(Subprocess&& other) noexcept;

    // Inicia o processo. Lança std::system_error em falha.
    void start(const StartInfo& si);

    // Escreve no stdin do filho (bloqueante). Retorna bytes escritos. 0 => pipe fechado pelo pai.
    // Lança std::system_error em falha.
    std::size_t write_stdin(const void* data, std::size_t size);

    // Fecha o stdin do filho (sinaliza EOF para o processo).
    void close_stdin() noexcept;

    // Leitura bloqueante de stdout. Lê até 'max_bytes' (se >0), mas pode retornar menos.
    // Retorna 0 em EOF. Lança std::system_error em falha.
    std::size_t read_stdout(void* buffer, std::size_t max_bytes);

    // Versão conveniente que lê uma linha (terminada por '\n').
    // Retorna false em EOF sem dados; caso contrário, 'line' recebe os bytes (com '\n' se houver).
    bool read_stdout_line(std::string& line);

    // Leitura bloqueante de stderr (se não redirecionado ao stdout).
    std::size_t read_stderr(void* buffer, std::size_t max_bytes);
    bool read_stderr_line(std::string& line);

    // Aguarda o término; retorna exit code. Bloqueia até o processo sair.
    // Lança std::system_error em falha.
    int wait();

    // Tenta terminar o processo (SIGTERM no POSIX / TerminateProcess no Windows).
    // (opcional) Depois chame wait().
    void terminate() noexcept;

    // Está rodando?
    bool running() const noexcept;

private:
    struct Impl;
    Impl* pimpl_;
};

} // namespace proc
