#include "CuriousMobController.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "BotPlayer.h"
#include "CuriousMobBridge.h"
#include "CuriousMobJson.h"

#include "../../../Minecraft.World/Blocks/Tile.h"
#include "../../../Minecraft.World/Containers/AbstractContainerMenu.h"
#include "../../../Minecraft.World/Containers/Inventory.h"
#include "../../../Minecraft.World/Entities/LivingEntity.h"
#include "../../../Minecraft.World/Items/ItemInstance.h"
#include "../../../Minecraft.World/Level/Level.h"
#include "../../../Minecraft.World/Player/FoodData.h"
#include "../../../Minecraft.World/WorldGen/Biomes/Biome.h"

using cmjson::JsonWriter;

namespace {

// Escreve um item (ou null) como objeto JSON compacto.
void writeItem(JsonWriter& w, const std::string& key,
               const std::shared_ptr<ItemInstance>& item) {
    if (item == nullptr || item->count <= 0) {
        w.nullField(key);
        return;
    }
    w.beginObject(key)
        .field("id", item->id)
        .field("count", item->count)
        .field("aux", item->getAuxValue())
        .endObject();
}

}  // namespace

CuriousMobController::CuriousMobController(BotPlayer* bot)
    : bot(bot),
      bridge(new CuriousMobBridge(5555)),
      tickCount(0),
      wanderTicksLeft(0),
      lastAttackHit(false),
      lastUseHit(false),
      lastBreakHit(false),
      lastPlaceHit(false) {}

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
        // Os flags de resultado descrevem o que aconteceu desde o último
        // estado enviado; zeramos só depois de reportá-los.
        lastAttackHit = lastUseHit = lastBreakHit = lastPlaceHit = false;
    }
}

void CuriousMobController::applyAction(const std::string& json) {
    // --- Movimento -------------------------------------------------------
    // Dois formatos aceitos: `move` categórico (mais simples para políticas
    // discretas) e `strafe`/`forward` contínuos (-1..1, o que a rede de uma
    // política contínua produz naturalmente). Os contínuos, se presentes,
    // têm precedência.
    float forward = 0.0f, strafe = 0.0f;
    std::string move = cmjson::getString(json, "move");
    if (move == "forward") {
        forward = 1.0f;
    } else if (move == "back") {
        forward = -1.0f;
    } else if (move == "left") {
        strafe = -1.0f;
    } else if (move == "right") {
        strafe = 1.0f;
    }
    if (cmjson::hasKey(json, "forward")) {
        forward = std::max(-1.0f, std::min(1.0f, cmjson::getFloat(json, "forward")));
    }
    if (cmjson::hasKey(json, "strafe")) {
        strafe = std::max(-1.0f, std::min(1.0f, cmjson::getFloat(json, "strafe")));
    }

    bot->setMoveInput(strafe, forward, cmjson::getBool(json, "jump"));
    bot->setSneaking(cmjson::getBool(json, "sneak"));
    bot->setSprinting(cmjson::getBool(json, "sprint"));

    // --- Mira ------------------------------------------------------------
    // `turn`/`look_pitch` em graus; `turn` também aceita "left"/"right" como
    // atalho categórico (passo fixo de 6°, como na Etapa 1).
    float yawDelta = cmjson::getFloat(json, "turn");
    std::string turn = cmjson::getString(json, "turn");
    if (turn == "left") {
        yawDelta = -6.0f;
    } else if (turn == "right") {
        yawDelta = 6.0f;
    }
    float pitchDelta = cmjson::getFloat(json, "look_pitch");
    if (yawDelta != 0.0f || pitchDelta != 0.0f) {
        bot->lookBy(yawDelta, pitchDelta);
    }

    // --- Inventário ------------------------------------------------------
    // Antes dos cliques: selecionar o slot é o que decide com que item o
    // ataque/uso do mesmo tick acontece, igual a um jogador rolando a hotbar
    // antes de clicar.
    if (cmjson::hasKey(json, "select_slot")) {
        bot->selectSlot(cmjson::getInt(json, "select_slot", -1));
    }
    if (cmjson::hasKey(json, "swap_from") && cmjson::hasKey(json, "swap_to")) {
        bot->swapInventorySlots(cmjson::getInt(json, "swap_from", -1),
                                cmjson::getInt(json, "swap_to", -1));
    }
    if (cmjson::getBool(json, "drop")) bot->dropSelected(false);
    if (cmjson::getBool(json, "drop_stack")) bot->dropSelected(true);
    if (cmjson::getBool(json, "drop_all")) bot->dropWholeInventory();

    // --- Cliques ---------------------------------------------------------
    if (cmjson::getBool(json, "attack")) lastAttackHit |= bot->attackTarget();
    if (cmjson::getBool(json, "use")) lastUseHit |= bot->useTarget();

    // Atalhos da Etapa 1, mantidos por compatibilidade do protocolo.
    if (cmjson::getBool(json, "break_block")) {
        lastBreakHit |= bot->breakBlockInFront();
    }
    if (cmjson::getBool(json, "place_block")) {
        lastPlaceHit |= bot->placeBlockInFront();
    }

    // --- Item em uso / container ----------------------------------------
    if (cmjson::getBool(json, "release_item")) bot->releaseItem();
    if (cmjson::getBool(json, "stop_item")) bot->cancelItem();
    if (cmjson::getBool(json, "close_container")) bot->closeContainerMenu();
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

// --- Serialização do estado ---------------------------------------------

std::string CuriousMobController::buildStateJson() {
    JsonWriter w;
    Level* level = bot->level;

    w.field("tick", tickCount)
        .field("x", bot->x)
        .field("y", bot->y)
        .field("z", bot->z)
        .field("yaw", static_cast<double>(bot->yRot))
        .field("pitch", static_cast<double>(bot->xRot))
        .field("vx", bot->xd)
        .field("vy", bot->yd)
        .field("vz", bot->zd);

    w.field("on_ground", bot->onGround)
        .field("in_water", bot->isInWater())
        .field("in_lava", bot->isInLava())
        .field("sneaking", bot->isSneaking())
        .field("sprinting", bot->isSprinting())
        .field("sleeping", bot->isSleeping())
        .field("using_item", bot->isUsingItem())
        .field("alive", bot->isAlive());

    w.field("health", static_cast<double>(bot->getHealth()))
        .field("max_health", static_cast<double>(bot->getMaxHealth()))
        .field("air", bot->getAirSupply())
        .field("xp_level", bot->experienceLevel)
        .field("xp_progress", static_cast<double>(bot->experienceProgress));

    FoodData* food = bot->getFoodData();
    w.field("food", food != nullptr ? food->getFoodLevel() : 0)
        .field("saturation",
               food != nullptr ? static_cast<double>(food->getSaturationLevel())
                               : 0.0);

    // Mundo
    if (level != nullptr) {
        w.field("day_time", static_cast<long long>(level->getDayTime()))
            .field("is_day", level->isDay())
            .field("light",
                   static_cast<double>(level->getBrightness(
                       static_cast<int>(bot->x), static_cast<int>(bot->y),
                       static_cast<int>(bot->z))));

        Biome* biome = level->getBiome(static_cast<int>(bot->x),
                                       static_cast<int>(bot->z));
        if (biome != nullptr) {
            w.beginObject("biome")
                .field("id", biome->id)
                .field("name", biome->m_name)
                .endObject();
        } else {
            w.nullField("biome");
        }
    } else {
        w.field("day_time", 0LL).field("is_day", true).field("light", 0.0);
        w.nullField("biome");
    }

    // Campo da Etapa 1, mantido para não quebrar clientes antigos.
    w.field("block_in_front", bot->getBlockIdInFront());

    writeTarget(w);
    writeSurroundings(w);
    writeNearestEntity(w);
    writeInventory(w);
    writeContainer(w);

    w.beginObject("last_result")
        .field("attack", lastAttackHit)
        .field("use", lastUseHit)
        .field("break", lastBreakHit)
        .field("place", lastPlaceHit)
        .endObject();

    return w.str();
}

void CuriousMobController::writeTarget(JsonWriter& w) {
    BotTarget t = bot->pickTarget();
    w.beginObject("target");

    if (t.kind == BotTarget::TILE && bot->level != nullptr) {
        int id = bot->level->getTile(t.x, t.y, t.z);
        w.field("type", "tile")
            .field("x", t.x)
            .field("y", t.y)
            .field("z", t.z)
            .field("face", t.face)
            .field("block", id)
            .field("data", bot->level->getData(t.x, t.y, t.z))
            .field("distance", t.distance);
    } else if (t.kind == BotTarget::ENTITY && t.entity != nullptr) {
        w.field("type", "entity")
            .field("entity_type", static_cast<int>(t.entity->GetType()))
            .field("entity_name", t.entity->getAName())
            .field("distance", t.distance);
        auto living = std::dynamic_pointer_cast<LivingEntity>(t.entity);
        if (living != nullptr) {
            w.field("entity_health", static_cast<double>(living->getHealth()));
        }
    } else {
        w.field("type", "none");
    }
    w.endObject();
}

void CuriousMobController::writeSurroundings(JsonWriter& w) {
    // Vizinhança imediata em coordenadas relativas à célula do bot. É o
    // mínimo para uma política aprender "tem parede à frente / buraco
    // abaixo" sem depender do raycast, que só devolve UM alvo.
    w.beginObject("blocks")
        .field("feet", bot->getTileAtOffset(0, 0, 0))
        .field("head", bot->getTileAtOffset(0, 1, 0))
        .field("below", bot->getTileAtOffset(0, -1, 0))
        .field("above", bot->getTileAtOffset(0, 2, 0))
        .field("north", bot->getTileAtOffset(0, 0, -1))
        .field("south", bot->getTileAtOffset(0, 0, 1))
        .field("west", bot->getTileAtOffset(-1, 0, 0))
        .field("east", bot->getTileAtOffset(1, 0, 0))
        .endObject();
}

void CuriousMobController::writeNearestEntity(JsonWriter& w) {
    std::shared_ptr<Entity> e = bot->getNearestEntity(16.0);
    if (e == nullptr) {
        w.nullField("nearest_entity");
        return;
    }
    double dx = e->x - bot->x, dy = e->y - bot->y, dz = e->z - bot->z;
    w.beginObject("nearest_entity")
        .field("type", static_cast<int>(e->GetType()))
        .field("name", e->getAName())
        .field("dx", dx)
        .field("dy", dy)
        .field("dz", dz)
        .field("distance", std::sqrt(dx * dx + dy * dy + dz * dz));
    auto living = std::dynamic_pointer_cast<LivingEntity>(e);
    if (living != nullptr) {
        w.field("health", static_cast<double>(living->getHealth()));
    }
    w.endObject();
}

void CuriousMobController::writeInventory(JsonWriter& w) {
    std::shared_ptr<Inventory> inv = bot->inventory;
    if (inv == nullptr) {
        w.nullField("inventory");
        return;
    }

    w.beginObject("inventory").field("selected", inv->selected);

    // Só slots ocupados: um inventário vazio serializado por extenso são 36
    // objetos "null" por mensagem, 4x por segundo, sem informação nenhuma.
    w.beginArray("items");
    for (unsigned int i = 0; i < inv->items.length; i++) {
        std::shared_ptr<ItemInstance> item = inv->items[i];
        if (item == nullptr || item->count <= 0) continue;
        w.beginObject()
            .field("slot", static_cast<int>(i))
            .field("id", item->id)
            .field("count", item->count)
            .field("aux", item->getAuxValue())
            .endObject();
    }
    w.endArray();

    w.beginArray("armor");
    for (unsigned int i = 0; i < inv->armor.length; i++) {
        std::shared_ptr<ItemInstance> item = inv->armor[i];
        if (item == nullptr || item->count <= 0) continue;
        w.beginObject()
            .field("slot", static_cast<int>(i))
            .field("id", item->id)
            .field("count", item->count)
            .field("aux", item->getAuxValue())
            .endObject();
    }
    w.endArray();

    writeItem(w, "held", inv->getSelected());
    w.endObject();
}

void CuriousMobController::writeContainer(JsonWriter& w) {
    if (!bot->hasContainerOpen() || bot->containerMenu == nullptr) {
        w.nullField("container");
        return;
    }

    w.beginObject("container")
        .field("size", static_cast<int>(bot->containerMenu->getSize()));
    w.beginArray("items");
    std::vector<std::shared_ptr<ItemInstance>>* items =
        bot->containerMenu->getItems();
    if (items != nullptr) {
        for (size_t i = 0; i < items->size(); i++) {
            std::shared_ptr<ItemInstance> item = (*items)[i];
            if (item == nullptr || item->count <= 0) continue;
            w.beginObject()
                .field("slot", static_cast<int>(i))
                .field("id", item->id)
                .field("count", item->count)
                .field("aux", item->getAuxValue())
                .endObject();
        }
        // getItems() devolve um vetor recém-alocado (o menu monta a lista a
        // partir dos seus Slots), então a posse é nossa.
        delete items;
    }
    w.endArray();
    w.endObject();
}
