#include "subprocess.hpp"
#include <stdexcept>
#include <vector>
#include <cstring>

#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
  #include <fcntl.h>
  #include <io.h>
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <errno.h>
  #include <signal.h>
#endif

namespace proc {

static std::system_error sys_err(const char* msg) {
#ifdef _WIN32
    return std::system_error((int)GetLastError(), std::system_category(), msg);
#else
    return std::system_error(errno, std::system_category(), msg);
#endif
}

struct Subprocess::Impl {
#ifdef _WIN32
    PROCESS_INFORMATION pi{};
    HANDLE hStdinWr = NULL;   // pai escreve
    HANDLE hStdoutRd = NULL;  // pai lê
    HANDLE hStderrRd = NULL;  // pai lê (opcional)
    bool started = false;

    static std::wstring to_w(const std::string& s) {
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring ws(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), len);
        return ws;
    }
#else
    pid_t pid = -1;
    int stdin_pipe[2]{-1,-1};
    int stdout_pipe[2]{-1,-1};
    int stderr_pipe[2]{-1,-1};
#endif
    bool stderr_redirected = false;

    void reset() {
#ifdef _WIN32
        if (pi.hThread) CloseHandle(pi.hThread);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        pi = PROCESS_INFORMATION{};
        if (hStdinWr) CloseHandle(hStdinWr);
        if (hStdoutRd) CloseHandle(hStdoutRd);
        if (hStderrRd) CloseHandle(hStderrRd);
        hStdinWr = hStdoutRd = hStderrRd = NULL;
        started = false;
#else
        if (stdin_pipe[0] != -1) { close(stdin_pipe[0]); stdin_pipe[0] = -1; }
        if (stdin_pipe[1] != -1) { close(stdin_pipe[1]); stdin_pipe[1] = -1; }
        if (stdout_pipe[0] != -1) { close(stdout_pipe[0]); stdout_pipe[0] = -1; }
        if (stdout_pipe[1] != -1) { close(stdout_pipe[1]); stdout_pipe[1] = -1; }
        if (stderr_pipe[0] != -1) { close(stderr_pipe[0]); stderr_pipe[0] = -1; }
        if (stderr_pipe[1] != -1) { close(stderr_pipe[1]); stderr_pipe[1] = -1; }
        pid = -1;
#endif
    }
    ~Impl() { reset(); }
};

Subprocess::Subprocess() : pimpl_(new Impl()) {}
Subprocess::~Subprocess() { delete pimpl_; }

Subprocess::Subprocess(Subprocess&& o) noexcept : pimpl_(o.pimpl_) { o.pimpl_ = nullptr; }
Subprocess& Subprocess::operator=(Subprocess&& o) noexcept {
    if (this != &o) { delete pimpl_; pimpl_ = o.pimpl_; o.pimpl_ = nullptr; }
    return *this;
}

void Subprocess::start(const StartInfo& si) {
    if (!pimpl_) pimpl_ = new Impl();
    pimpl_->reset();
    pimpl_->stderr_redirected = si.redirect_stderr_to_stdout;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE childStdinRd = NULL, childStdinWr = NULL;
    HANDLE childStdoutRd = NULL, childStdoutWr = NULL;
    HANDLE childStderrRd = NULL, childStderrWr = NULL;

    if (!CreatePipe(&childStdoutRd, &childStdoutWr, &sa, 0)) throw sys_err("CreatePipe(stdout)");
    if (!SetHandleInformation(childStdoutRd, HANDLE_FLAG_INHERIT, 0)) throw sys_err("SetHandleInformation(stdoutRd)");

    if (!CreatePipe(&childStdinRd, &childStdinWr, &sa, 0)) { CloseHandle(childStdoutRd); CloseHandle(childStdoutWr); throw sys_err("CreatePipe(stdin)"); }
    if (!SetHandleInformation(childStdinWr, HANDLE_FLAG_INHERIT, 0)) { CloseHandle(childStdoutRd); CloseHandle(childStdoutWr); CloseHandle(childStdinRd); CloseHandle(childStdinWr); throw sys_err("SetHandleInformation(stdinWr)"); }

    if (!si.redirect_stderr_to_stdout) {
        if (!CreatePipe(&childStderrRd, &childStderrWr, &sa, 0)) { CloseHandle(childStdoutRd); CloseHandle(childStdoutWr); CloseHandle(childStdinRd); CloseHandle(childStdinWr); throw sys_err("CreatePipe(stderr)"); }
        if (!SetHandleInformation(childStderrRd, HANDLE_FLAG_INHERIT, 0)) { CloseHandle(childStdoutRd); CloseHandle(childStdoutWr); CloseHandle(childStdinRd); CloseHandle(childStdinWr); CloseHandle(childStderrRd); CloseHandle(childStderrWr); throw sys_err("SetHandleInformation(stderrRd)"); }
    }

    STARTUPINFOW siw{};
    siw.cb = sizeof(siw);
    siw.dwFlags |= STARTF_USESTDHANDLES;
    siw.hStdInput  = childStdinRd;
    siw.hStdOutput = childStdoutWr;
    siw.hStdError  = si.redirect_stderr_to_stdout ? childStdoutWr : childStderrWr;

    // Monta a command line no formato: "program" "arg1" "arg2" ...
    std::wstring cmd;
    auto append_quoted = [&](const std::string& s){
        std::wstring w = Impl::to_w(s);
        cmd.push_back(L'"');
        cmd += w;
        cmd.push_back(L'"');
    };
    append_quoted(si.program);
    for (const auto& a : si.args) {
        cmd.push_back(L' ');
        append_quoted(a);
    }

    std::wstring wdir = si.working_dir.empty() ? std::wstring() : Impl::to_w(si.working_dir);

    BOOL ok = CreateProcessW(
        nullptr,             // application name
        cmd.data(),          // command line (modificável)
        nullptr, nullptr,
        TRUE,                // herdar handles
        0,
        nullptr,
        si.working_dir.empty() ? nullptr : wdir.c_str(),
        &siw,
        &pimpl_->pi
    );

    // Fechar no PAI as extremidades herdadas que não usa mais
    CloseHandle(childStdoutWr);
    CloseHandle(childStdinRd);
    if (!si.redirect_stderr_to_stdout) CloseHandle(childStderrWr);

    if (!ok) {
        CloseHandle(childStdoutRd); CloseHandle(childStdinWr);
        if (!si.redirect_stderr_to_stdout) CloseHandle(childStderrRd);
        throw sys_err("CreateProcessW");
    }

    pimpl_->hStdoutRd = childStdoutRd;
    pimpl_->hStdinWr  = childStdinWr;
    pimpl_->hStderrRd = si.redirect_stderr_to_stdout ? NULL : childStderrRd;
    pimpl_->started = TRUE;

#else
    if (pipe(pimpl_->stdin_pipe)  == -1) throw sys_err("pipe(stdin)");
    if (pipe(pimpl_->stdout_pipe) == -1) throw sys_err("pipe(stdout)");
    if (!si.redirect_stderr_to_stdout) {
        if (pipe(pimpl_->stderr_pipe) == -1) throw sys_err("pipe(stderr)");
    }

    pid_t pid = fork();
    if (pid < 0) throw sys_err("fork");

    if (pid == 0) {
        // FILHO
        // redireciona
        dup2(pimpl_->stdin_pipe[0],  STDIN_FILENO);
        dup2(pimpl_->stdout_pipe[1], STDOUT_FILENO);
        if (si.redirect_stderr_to_stdout) {
            dup2(pimpl_->stdout_pipe[1], STDERR_FILENO);
        } else {
            dup2(pimpl_->stderr_pipe[1], STDERR_FILENO);
        }

        // fecha descritores supérfluos
        close(pimpl_->stdin_pipe[1]);
        close(pimpl_->stdout_pipe[0]);
        if (!si.redirect_stderr_to_stdout) close(pimpl_->stderr_pipe[0]);

        // working dir
        if (!si.working_dir.empty()) {
            if (chdir(si.working_dir.c_str()) != 0) {
                const char* msg = "chdir failed\n"; write(STDERR_FILENO, msg, strlen(msg)); _exit(127);
            }
        }

        // monta argv
        std::vector<char*> argv;
        argv.reserve(si.args.size() + 2);
        argv.push_back(const_cast<char*>(si.program.c_str())); // argv[0]
        for (const auto& a : si.args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        execv(si.program.c_str(), argv.data());
        // se falhar:
        const char* msg = "execv failed\n"; write(STDERR_FILENO, msg, strlen(msg)); _exit(127);
    }

    // PAI
    pimpl_->pid = pid;
    // fecha extremidades não usadas
    close(pimpl_->stdin_pipe[0]);
    close(pimpl_->stdout_pipe[1]);
    if (!si.redirect_stderr_to_stdout) close(pimpl_->stderr_pipe[1]);
#endif
}

std::size_t Subprocess::write_stdin(const void* data, std::size_t size) {
#ifdef _WIN32
    if (!pimpl_->hStdinWr) return 0;
    DWORD written = 0;
    if (!WriteFile(pimpl_->hStdinWr, data, (DWORD)size, &written, NULL)) throw sys_err("WriteFile(stdin)");
    return (std::size_t)written;
#else
    if (pimpl_->stdin_pipe[1] == -1) return 0;
    ssize_t w = ::write(pimpl_->stdin_pipe[1], data, size);
    if (w < 0) throw sys_err("write(stdin)");
    return (std::size_t)w;
#endif
}

void Subprocess::close_stdin() noexcept {
#ifdef _WIN32
    if (pimpl_->hStdinWr) { CloseHandle(pimpl_->hStdinWr); pimpl_->hStdinWr = NULL; }
#else
    if (pimpl_->stdin_pipe[1] != -1) { close(pimpl_->stdin_pipe[1]); pimpl_->stdin_pipe[1] = -1; }
#endif
}

static bool read_line_from_stream_blocking(
#ifdef _WIN32
    HANDLE h, 
#else
    int fd,
#endif
    std::string& line_out)
{
    line_out.clear();
    char ch;
    for (;;) {
#ifdef _WIN32
        DWORD n = 0;
        BOOL ok = ReadFile(h, &ch, 1, &n, NULL);
        if (!ok) return !line_out.empty(); // erro/EOF: se já tem algo, devolve
        if (n == 0) return !line_out.empty(); // EOF
#else
        ssize_t n = ::read(fd, &ch, 1);
        if (n == 0) return !line_out.empty(); // EOF
        if (n < 0) {
            if (errno == EINTR) continue;
            return !line_out.empty();
        }
#endif
        line_out.push_back(ch);
        if (ch == '\n') return true;
    }
}

std::size_t Subprocess::read_stdout(void* buffer, std::size_t max_bytes) {
#ifdef _WIN32
    if (!pimpl_->hStdoutRd) return 0;
    DWORD n = 0;
    if (!ReadFile(pimpl_->hStdoutRd, buffer, (DWORD)max_bytes, &n, NULL)) {
        // erro ou EOF
        return 0;
    }
    return (std::size_t)n;
#else
    if (pimpl_->stdout_pipe[0] == -1) return 0;
    for (;;) {
        ssize_t n = ::read(pimpl_->stdout_pipe[0], buffer, max_bytes);
        if (n > 0) return (std::size_t)n;
        if (n == 0) return 0; // EOF
        if (errno == EINTR) continue;
        throw sys_err("read(stdout)");
    }
#endif
}

bool Subprocess::read_stdout_line(std::string& line) {
#ifdef _WIN32
    if (!pimpl_->hStdoutRd) return false;
    return read_line_from_stream_blocking(pimpl_->hStdoutRd, line);
#else
    if (pimpl_->stdout_pipe[0] == -1) return false;
    return read_line_from_stream_blocking(pimpl_->stdout_pipe[0], line);
#endif
}

std::size_t Subprocess::read_stderr(void* buffer, std::size_t max_bytes) {
    if (pimpl_->stderr_redirected) return 0;
#ifdef _WIN32
    if (!pimpl_->hStderrRd) return 0;
    DWORD n = 0;
    if (!ReadFile(pimpl_->hStderrRd, buffer, (DWORD)max_bytes, &n, NULL)) return 0;
    return (std::size_t)n;
#else
    if (pimpl_->stderr_pipe[0] == -1) return 0;
    for (;;) {
        ssize_t n = ::read(pimpl_->stderr_pipe[0], buffer, max_bytes);
        if (n > 0) return (std::size_t)n;
        if (n == 0) return 0;
        if (errno == EINTR) continue;
        throw sys_err("read(stderr)");
    }
#endif
}

bool Subprocess::read_stderr_line(std::string& line) {
    if (pimpl_->stderr_redirected) return false;
#ifdef _WIN32
    if (!pimpl_->hStderrRd) return false;
    return read_line_from_stream_blocking(pimpl_->hStderrRd, line);
#else
    if (pimpl_->stderr_pipe[0] == -1) return false;
    return read_line_from_stream_blocking(pimpl_->stderr_pipe[0], line);
#endif
}

int Subprocess::wait() {
#ifdef _WIN32
    if (!pimpl_->pi.hProcess) throw sys_err("wait: not started");
    WaitForSingleObject(pimpl_->pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pimpl_->pi.hProcess, &code);
    return (int)code;
#else
    if (pimpl_->pid <= 0) throw sys_err("wait: not started");
    int status = 0;
    if (waitpid(pimpl_->pid, &status, 0) < 0) throw sys_err("waitpid");
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
}

void Subprocess::terminate() noexcept {
#ifdef _WIN32
    if (pimpl_->pi.hProcess) TerminateProcess(pimpl_->pi.hProcess, 1);
#else
    if (pimpl_->pid > 0) kill(pimpl_->pid, SIGTERM);
#endif
}

bool Subprocess::running() const noexcept {
#ifdef _WIN32
    if (!pimpl_->pi.hProcess) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(pimpl_->pi.hProcess, &code)) return false;
    return code == STILL_ACTIVE;
#else
    if (pimpl_->pid <= 0) return false;
    int status = 0;
    pid_t r = waitpid(pimpl_->pid, &status, WNOHANG);
    return r == 0;
#endif
}

} // namespace proc
