#include "BotPlayer.h"
#include "CuriousMobController.h"

#include <cmath>
#include <cstdio>

#include "../../../Minecraft.World/Blocks/Tile.h"
#include "../../../Minecraft.World/Items/ItemInstance.h"
#include "../../../Minecraft.World/Level/Level.h"
#include "../../../Minecraft.World/Util/AABB.h"
#include "../../../Minecraft.World/Util/Vec3.h"

BotPlayer::BotPlayer(Level* level, const std::wstring& name)
    : Player(level, name), controller(nullptr) {
    // O bot nunca passa por GameMode::initPlayer (que é quem aplicaria o
    // modo de jogo do mundo, ex. criativo), então abilities já nasceria em
    // survival por padrão. Fixamos explicitamente aqui para não depender
    // desse acaso: o bot deve continuar em sobrevivência mesmo que o mundo
    // seja criado/aberto em modo criativo.
    abilities.invulnerable = false;
    abilities.flying = false;
    abilities.mayfly = false;
    abilities.instabuild = false;
    abilities.mayBuild = true;

    fprintf(stderr, "[CuriousMob] BotPlayer construido\n");
    controller = new CuriousMobController(this);
}

BotPlayer::~BotPlayer() { delete controller; }

void BotPlayer::tick() {
    // Diferente de um LocalPlayer real (cujo travel()/jumpFromGround() são
    // disparados externamente pelo loop de input do jogador local), o
    // BotPlayer roda no lado servidor (isEffectiveAi() == true para um
    // Player não-local), então LivingEntity::aiStep() — chamado de dentro
    // de Player::tick() logo abaixo — já aplica travel(xxa, yya) e o pulo
    // sozinho. Só precisamos setar xxa/yya/jumping/yRot a partir da ação da
    // ponte (ou do fallback aleatório) antes de chamar Player::tick();
    // chamar travel()/jumpFromGround() aqui também duplicava o movimento
    // (o bot andava ~2x mais rápido que um jogador real).
    if (controller != nullptr) controller->tick();

    Player::tick();
}

void BotPlayer::setMoveInput(float strafe, float forward, bool jump) {
    xxa = strafe;
    yya = forward;
    setJumping(jump);
}

void BotPlayer::turnBy(float yawDelta) { setRot(yRot + yawDelta, xRot); }

bool BotPlayer::getBlockInFrontPos(int& outX, int& outY, int& outZ) {
    std::optional<Vec3> look = getLookAngle();
    if (!look.has_value()) return false;

    double eyeY = y + getHeadHeight();
    const double reach = 1.5;

    outX = static_cast<int>(std::floor(x + look->x * reach));
    outY = static_cast<int>(std::floor(eyeY + look->y * reach));
    outZ = static_cast<int>(std::floor(z + look->z * reach));
    return true;
}

int BotPlayer::getBlockIdInFront() {
    int bx, by, bz;
    if (!getBlockInFrontPos(bx, by, bz)) return -1;
    if (level == nullptr) return -1;
    return level->getTile(bx, by, bz);
}

bool BotPlayer::breakBlockInFront() {
    int bx, by, bz;
    if (!getBlockInFrontPos(bx, by, bz)) return false;
    if (level == nullptr) return false;

    int tileId = level->getTile(bx, by, bz);
    if (tileId == 0) return false;  // ar, nada a quebrar

    return level->destroyTile(bx, by, bz, /*dropResources=*/true);
}

bool BotPlayer::placeBlockInFront() {
    int bx, by, bz;
    if (!getBlockInFrontPos(bx, by, bz)) return false;
    if (level == nullptr) return false;

    if (level->getTile(bx, by, bz) != 0) return false;  // célula já ocupada

    std::shared_ptr<ItemInstance> held = getSelectedItem();
    if (held == nullptr) return false;

    int tileId = held->id;
    if (tileId < 0 || Tile::tiles == nullptr || Tile::tiles[tileId] == nullptr) {
        return false;  // item selecionado não representa um bloco
    }

    return level->setTileAndData(bx, by, bz, tileId, 0, Tile::UPDATE_ALL);
}

bool BotPlayer::attackNearestEntity(double radius) {
    if (level == nullptr) return false;

    AABB box(x - radius, y - radius, z - radius, x + radius, y + radius,
             z + radius);
    std::shared_ptr<Entity> self =
        std::dynamic_pointer_cast<Entity>(shared_from_this());

    std::vector<std::shared_ptr<Entity>>* nearby =
        level->getEntities(self, &box);
    if (nearby == nullptr) return false;

    std::shared_ptr<Entity> target = nullptr;
    double bestDist = radius * radius;
    for (auto& e : *nearby) {
        if (e == nullptr || e.get() == this) continue;
        double dx = e->x - x, dy = e->y - y, dz = e->z - z;
        double dist = dx * dx + dy * dy + dz * dz;
        if (dist < bestDist) {
            bestDist = dist;
            target = e;
        }
    }
    delete nearby;

    if (target == nullptr) return false;
    attack(target);
    return true;
}
