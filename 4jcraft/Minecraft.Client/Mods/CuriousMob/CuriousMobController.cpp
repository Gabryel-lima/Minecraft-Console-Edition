#include "CuriousMobController.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "BotPlayer.h"
#include "CuriousMobBridge.h"

namespace {

// Parsing manual e propositalmente simples: o schema das mensagens de ação
// é fixo (ver mods/CuriousMob/protocol/messages.md), então não é preciso um
// parser JSON completo — só extrair alguns campos string/bool de um objeto
// plano.

std::string jsonStringField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    size_t firstQuote = json.find('"', pos + 1);
    if (firstQuote == std::string::npos) return "";
    size_t secondQuote = json.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos) return "";
    return json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

bool jsonBoolField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;

    size_t truePos = json.find("true", pos);
    size_t commaPos = json.find(',', pos);
    size_t bracePos = json.find('}', pos);
    size_t end = std::min(commaPos, bracePos);

    return truePos != std::string::npos && truePos < end;
}

}  // namespace

CuriousMobController::CuriousMobController(BotPlayer* bot)
    : bot(bot),
      bridge(new CuriousMobBridge(5555)),
      tickCount(0),
      wanderTicksLeft(0) {}

CuriousMobController::~CuriousMobController() { delete bridge; }

void CuriousMobController::tick() {
    tickCount++;

    auto action = bridge->pollAction();
    if (action.has_value()) {
        applyAction(*action);
    } else {
        applyRandomWander();
    }

    if (tickCount % STATE_SEND_INTERVAL == 0) {
        bridge->sendState(buildStateJson());
    }
}

void CuriousMobController::applyAction(const std::string& json) {
    std::string move = jsonStringField(json, "move");
    std::string turn = jsonStringField(json, "turn");
    bool jump = jsonBoolField(json, "jump");
    bool doBreak = jsonBoolField(json, "break_block");
    bool doPlace = jsonBoolField(json, "place_block");
    bool doAttack = jsonBoolField(json, "attack");

    float forward = 0.0f, strafe = 0.0f;
    if (move == "forward") {
        forward = 1.0f;
    } else if (move == "back") {
        forward = -1.0f;
    } else if (move == "left") {
        strafe = -1.0f;
    } else if (move == "right") {
        strafe = 1.0f;
    }
    bot->setMoveInput(strafe, forward, jump);

    if (turn == "left") {
        bot->turnBy(-6.0f);
    } else if (turn == "right") {
        bot->turnBy(6.0f);
    }

    if (doBreak) bot->breakBlockInFront();
    if (doPlace) bot->placeBlockInFront();
    if (doAttack) bot->attackNearestEntity(3.0);
}

void CuriousMobController::applyRandomWander() {
    if (wanderTicksLeft <= 0) {
        wanderTicksLeft = 40 + std::rand() % 80;
        float delta = static_cast<float>((std::rand() % 90) - 45);
        bot->turnBy(delta);
    }
    wanderTicksLeft--;
    bot->setMoveInput(0.0f, 1.0f, false);
}

std::string CuriousMobController::buildStateJson() {
    std::ostringstream out;
    out << "{"
        << "\"tick\":" << tickCount << ","
        << "\"x\":" << bot->x << ","
        << "\"y\":" << bot->y << ","
        << "\"z\":" << bot->z << ","
        << "\"yaw\":" << bot->yRot << ","
        << "\"pitch\":" << bot->xRot << ","
        << "\"on_ground\":" << (bot->onGround ? "true" : "false") << ","
        << "\"health\":" << bot->getHealth() << ","
        << "\"food\":" << bot->getFoodData()->getFoodLevel() << ","
        << "\"block_in_front\":" << bot->getBlockIdInFront() << "}";
    return out.str();
}
