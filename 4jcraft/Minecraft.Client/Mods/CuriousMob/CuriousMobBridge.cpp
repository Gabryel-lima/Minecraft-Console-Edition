#include "CuriousMobBridge.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

CuriousMobBridge::CuriousMobBridge(int port)
    : port(port), listenFd(-1), clientFd(-1), running(true) {
    fprintf(stderr, "[CuriousMob] bridge: iniciando na porta %d\n", port);

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        fprintf(stderr, "[CuriousMob] bridge: socket() falhou (errno=%d)\n", errno);
        return;
    }

    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "[CuriousMob] bridge: bind() falhou (errno=%d)\n", errno);
        close(listenFd);
        listenFd = -1;
        return;
    }
    if (listen(listenFd, 1) < 0) {
        fprintf(stderr, "[CuriousMob] bridge: listen() falhou (errno=%d)\n", errno);
        close(listenFd);
        listenFd = -1;
        return;
    }

    fprintf(stderr, "[CuriousMob] bridge: escutando em 127.0.0.1:%d\n", port);
    acceptThread = std::thread(&CuriousMobBridge::acceptLoop, this);
}

CuriousMobBridge::~CuriousMobBridge() {
    running = false;

    if (listenFd >= 0) {
        shutdown(listenFd, SHUT_RDWR);
        close(listenFd);
    }
    int fd = clientFd.load();
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }

    if (acceptThread.joinable()) acceptThread.join();
    if (readThread.joinable()) readThread.join();
}

void CuriousMobBridge::acceptLoop() {
    if (listenFd < 0) return;

    while (running) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int fd = accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr),
                        &clientLen);
        if (fd < 0) {
            if (!running) return;
            continue;
        }

        // Só um cliente por vez (um bot, um processo Python) — descarta
        // conexões antigas.
        int old = clientFd.exchange(fd);
        if (old >= 0) close(old);

        if (readThread.joinable()) readThread.join();
        readThread = std::thread(&CuriousMobBridge::readLoop, this, fd);
    }
}

void CuriousMobBridge::readLoop(int fd) {
    std::string buffer;
    char chunk[512];

    while (running && clientFd.load() == fd) {
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            clientFd.compare_exchange_strong(fd, -1);
            close(fd);
            return;
        }
        buffer.append(chunk, static_cast<size_t>(n));

        size_t newlinePos;
        while ((newlinePos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newlinePos);
            buffer.erase(0, newlinePos + 1);
            if (!line.empty()) {
                std::lock_guard<std::mutex> lock(actionMutex);
                latestAction = line;
            }
        }
    }
}

void CuriousMobBridge::sendState(const std::string& jsonLine) {
    int fd = clientFd.load();
    if (fd < 0) return;

    std::string withNewline = jsonLine + "\n";
    // Best-effort: se o cliente caiu no meio, o próximo recv() no readLoop
    // detecta e limpa clientFd; aqui só evitamos travar com MSG_NOSIGNAL.
    send(fd, withNewline.c_str(), withNewline.size(), MSG_NOSIGNAL);
}

std::optional<std::string> CuriousMobBridge::pollAction() {
    std::lock_guard<std::mutex> lock(actionMutex);
    std::optional<std::string> result = latestAction;
    latestAction.reset();
    return result;
}

bool CuriousMobBridge::isConnected() const { return clientFd.load() >= 0; }
