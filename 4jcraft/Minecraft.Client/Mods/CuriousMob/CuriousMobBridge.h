#pragma once

// Ponte TCP simples entre o jogo e o processo Python externo.
// Ver mods/CuriousMob/protocol/messages.md para o formato das mensagens.
//
// O jogo atua como SERVIDOR local (127.0.0.1:<port>). O código de rede
// vanilla (Minecraft.World/Network/Socket.h) é acoplado ao protocolo QNet do
// console e não é reaproveitável aqui — este é um socket POSIX comum,
// escrito do zero, Linux-only por enquanto (plataforma de desenvolvimento).
//
// Leitura de ações é não bloqueante do ponto de vista do tick do jogo:
// roda numa thread própria, e pollAction() só consulta o último valor lido.

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class CuriousMobBridge {
public:
    explicit CuriousMobBridge(int port = 5555);
    ~CuriousMobBridge();

    // Envia uma linha de estado (sem o '\n' final) para o cliente conectado,
    // se houver. Best-effort: não bloqueia nem lança se não houver cliente.
    void sendState(const std::string& jsonLine);

    // Retorna (e consome) a última linha de ação recebida desde a última
    // chamada, ou std::nullopt se nada de novo chegou.
    std::optional<std::string> pollAction();

    bool isConnected() const;

private:
    void acceptLoop();
    void readLoop(int fd);

    int port;
    int listenFd;
    std::atomic<int> clientFd;
    std::atomic<bool> running;

    std::thread acceptThread;
    std::thread readThread;

    mutable std::mutex actionMutex;
    std::optional<std::string> latestAction;
};
