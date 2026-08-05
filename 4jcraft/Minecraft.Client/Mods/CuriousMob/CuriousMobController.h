#pragma once

// Liga o BotPlayer à CuriousMobBridge: a cada tick, aplica a última ação
// recebida do processo Python (ou anda em direção aleatória, se não houver
// ponte conectada) e periodicamente envia o estado atual do bot.

#include <string>

class BotPlayer;
class CuriousMobBridge;

class CuriousMobController {
public:
    explicit CuriousMobController(BotPlayer* bot);
    ~CuriousMobController();

    void tick();

private:
    static const int STATE_SEND_INTERVAL = 5;  // ticks

    BotPlayer* bot;
    CuriousMobBridge* bridge;

    long long tickCount;
    int wanderTicksLeft;

    void applyAction(const std::string& json);
    void applyRandomWander();
    std::string buildStateJson();
};
