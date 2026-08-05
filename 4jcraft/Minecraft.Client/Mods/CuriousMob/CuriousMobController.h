#pragma once

// Liga o BotPlayer à CuriousMobBridge: a cada tick, aplica a última ação
// recebida do processo Python (ou anda em direção aleatória, se não houver
// ponte conectada) e periodicamente envia o estado atual do bot.
//
// Divisão de responsabilidade: o BotPlayer sabe FAZER as coisas (com a mesma
// semântica de um jogador real); o controller só traduz JSON <-> chamadas.

#include <string>

class BotPlayer;
class CuriousMobBridge;

namespace cmjson {
class JsonWriter;
}

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

    // Contadores de resultado do último tick, devolvidos no estado seguinte
    // para o Python conseguir aprender com o efeito das próprias ações
    // (recompensa/insucesso) sem ter que inferir da posição.
    bool lastAttackHit;
    bool lastUseHit;
    bool lastBreakHit;
    bool lastPlaceHit;

    void applyAction(const std::string& json);
    void applyRandomWander();

    std::string buildStateJson();
    void writeTarget(cmjson::JsonWriter& w);
    void writeSurroundings(cmjson::JsonWriter& w);
    void writeInventory(cmjson::JsonWriter& w);
    void writeContainer(cmjson::JsonWriter& w);
    void writeNearestEntity(cmjson::JsonWriter& w);
};
