#include "BotPlayer.h"
#include "CuriousMobController.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "../../../Minecraft.World/Blocks/Tile.h"
#include "../../../Minecraft.World/Containers/AbstractContainerMenu.h"
#include "../../../Minecraft.World/Containers/Inventory.h"
#include "../../../Minecraft.World/Entities/ItemEntity.h"
#include "../../../Minecraft.World/Entities/LivingEntity.h"
#include "../../../Minecraft.World/Items/Item.h"
#include "../../../Minecraft.World/Items/ItemInstance.h"
#include "../../../Minecraft.World/Level/GameRules.h"
#include "../../../Minecraft.World/Level/Level.h"
#include "../../../Minecraft.World/Player/FoodData.h"
#include "../../../Minecraft.World/Util/AABB.h"
#include "../../../Minecraft.World/Util/HitResult.h"
#include "../../../Minecraft.World/Util/Pos.h"
#include "../../../Minecraft.World/Util/Vec3.h"
#include "../../Level/ServerLevel.h"
#include "../../MinecraftServer.h"
#include "../../Player/EntityTracker.h"

BotPlayer::BotPlayer(Level* level, const std::wstring& name)
    : Player(level, name), controller(nullptr) {
    // O bot nunca passa por GameMode::initPlayer (que é quem aplicaria o
    // modo de jogo do mundo, ex. criativo), então abilities já nasceria em
    // survival por padrão. Fixamos explicitamente aqui para não depender
    // desse acaso: o bot deve continuar em sobrevivência mesmo que o mundo
    // seja criado/aberto em modo criativo.
    // Convenção de posição do lado servidor (ver setDefaultHeadHeight() em
    // BotPlayer.h): `y` na altura dos pés, não dos olhos. O construtor de
    // Player já rodou um moveTo() com o 1.62 herdado, mas quem posiciona o bot
    // de verdade é o moveTo() de CuriousMobTickPendingSpawn(), logo depois
    // desta linha - igual ao ServerPlayer, que também zera o offset antes do
    // seu moveTo().
    heightOffset = 0;

    enforceSurvivalMode();

    // Sistema de confiança (privilégios): sem passar pela rede, o bot nasceria
    // sem nenhum privilégio concedido, e isAllowedToMine()/isAllowedToUse()
    // recusariam quebrar bloco, abrir baú e usar porta em mundos com o
    // sistema ligado. Concedemos tudo — o bot é um jogador local de
    // desenvolvimento, não um convidado remoto.
    enableAllPlayerPrivileges(true);

    fprintf(stderr, "[CuriousMob] BotPlayer construido\n");
    controller = new CuriousMobController(this);
}

BotPlayer::~BotPlayer() { delete controller; }

void BotPlayer::enforceSurvivalMode() {
    abilities.invulnerable = false;
    abilities.flying = false;
    abilities.mayfly = false;
    abilities.instabuild = false;
    abilities.mayBuild = true;
}

void BotPlayer::tick() {
    // Espelha o "Respawn" que um jogador real manda por packet depois da
    // tela de morte: sem isso, die() (chamado de dentro de hurt(), antes
    // deste tick) já dropou o inventário e zerou a vida, mas o bot ficava
    // parado ali para sempre, porque nada equivalente ao
    // PERFORM_RESPAWN/PlayerList::respawn existe pra um Player sem
    // PlayerConnection.
    if (getHealth() <= 0) {
        fprintf(stderr,
                "[CuriousMob] bot morreu (health=%.2f) em (%.2f,%.2f,%.2f), "
                "respawnando\n",
                getHealth(), x, y, z);
        respawnAfterDeath();
        return;
    }

    // DIAGNÓSTICO TEMPORÁRIO - remover quando o comportamento estiver
    // confirmado. Só loga quando o bot está ferido (a situação de interesse),
    // no máximo ~1x por segundo, pra não inundar o console.
    static int s_curiousMobDiagCounter = 0;
    if (getHealth() < getMaxHealth() && ++s_curiousMobDiagCounter % 20 == 0) {
        FoodData* food = getFoodData();
        fprintf(stderr,
                "[CuriousMob][diag] health=%.2f/%.2f food=%d sat=%.1f "
                "onGround=%d y=%.2f invulnerableTime=%d\n",
                getHealth(), getMaxHealth(),
                food != nullptr ? food->getFoodLevel() : -1,
                food != nullptr ? food->getSaturationLevel() : -1.0f, onGround,
                y, invulnerableTime);
    }

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

void BotPlayer::respawnAfterDeath() {
    Pos* bed = getRespawnPosition();  // dono é o próprio Player, não deletar.
    Pos* bedStandUp =
        bed != nullptr
            ? checkBedValidRespawnPosition(level, bed, isRespawnForced())
            : nullptr;

    if (bedStandUp != nullptr) {
        moveTo(bedStandUp->x + 0.5, bedStandUp->y + 0.1, bedStandUp->z + 0.5,
               0, 0);
    } else {
        Pos* worldSpawn = level->getSharedSpawnPos();
        moveTo(worldSpawn->x + 0.5, worldSpawn->y + 0.1, worldSpawn->z + 0.5,
               0, 0);
        delete worldSpawn;
        if (bed != nullptr) {
            // Cama não é mais válida (destruída/bloqueada) - mesmo aviso que
            // o jogador real recebe (GameEventPacket::NO_RESPAWN_BED_AVAILABLE).
            setRespawnPosition(nullptr, false);
        }
    }

    setSize(0.6f, 1.8f);
    setDefaultHeadHeight();

    // getSharedSpawnPos() devolve a coluna do spawn, não uma posição livre:
    // se houver terreno ali (o spawn compartilhado costuma cair dentro do
    // relevo depois que o mundo é gerado/modificado), o bot renascia DENTRO
    // do bloco e sufocava - 1 de dano por tick, morrendo e renascendo no
    // mesmo ponto para sempre. PlayerList::respawn() resolve isso do mesmo
    // jeito: sobe até não colidir com nada. O limite de iterações é só uma
    // trava contra loop infinito (coluna sólida até o teto do mundo).
    for (int attempt = 0; attempt < 256; attempt++) {
        auto* cubes = level->getCubes(shared_from_this(), &bb);
        if (cubes == nullptr || cubes->empty()) break;
        setPos(x, y + 1, z);
    }
    setHealth(getMaxHealth());
    foodData = FoodData();
    clearFire();
    fallDistance = 0;
    dead = false;
    enforceSurvivalMode();

    // Player::die() já esvaziou o inventário (dropAll) quando keepInventory
    // está desligado, mas não mexe em XP - isso normalmente é perdido porque
    // o respawn de verdade recria o Player do zero (PlayerList::respawn) e só
    // copia o XP antigo se keepInventory estiver ligado (Player::restoreFrom).
    // Replicamos a mesma regra aqui.
    if (!level->getGameRules()->getBoolean(GameRules::RULE_KEEPINVENTORY)) {
        experienceLevel = 0;
        totalExperience = 0;
        experienceProgress = 0;
    }

    // Ressincroniza o espelho client-side. Quando a vida chega a zero,
    // LivingEntity::hurt() manda EntityEvent::DEATH para todos os clientes que
    // acompanham a entidade; o cliente responde com setHealth(0) + die() e,
    // 20 ticks depois (a animação de tombar), APAGA o RemotePlayer do mundo
    // dele. Do lado servidor o bot renasce e continua vivo, mas o cliente
    // nunca mais recebe um pacote de spawn - resultado: bot invisível e
    // aparentemente parado no lugar onde morreu. Um jogador de verdade não
    // sofre disso porque PlayerList::respawn() destrói e recria o
    // ServerPlayer inteiro, e o novo passa pelo tracker do zero. Fazemos o
    // equivalente mínimo: tira do tracker (RemoveEntitiesPacket) e põe de
    // volta (AddPlayerPacket na posição nova).
    ServerLevel* serverLevel = dynamic_cast<ServerLevel*>(level);
    if (serverLevel != nullptr) {
        EntityTracker* tracker = serverLevel->getTracker();
        if (tracker != nullptr) {
            std::shared_ptr<Entity> self = shared_from_this();
            int oldEntityId = entityId;
            tracker->removeEntity(self);
            // O RemoveEntitiesPacket do ID antigo ainda está pendente nos
            // jogadores que viam o bot. Use um ID novo antes do AddPlayerPacket
            // para que a remoção atrasada não apague o RemotePlayer recriado.
            resetSmallId();
            enforceSurvivalMode();
            tracker->addEntity(self);
            fprintf(stderr,
                    "[CuriousMob] bot entityId trocado no respawn: %d -> %d\n",
                    oldEntityId, entityId);
        }
    }

    fprintf(stderr, "[CuriousMob] bot respawnou em (%.2f,%.2f,%.2f)\n", x, y,
            z);
}

std::shared_ptr<Player> BotPlayer::selfAsPlayer() {
    return std::dynamic_pointer_cast<Player>(shared_from_this());
}

// --- Input --------------------------------------------------------------

void BotPlayer::setMoveInput(float strafe, float forward, bool jump) {
    xxa = strafe;
    yya = forward;
    setJumping(jump);
}

void BotPlayer::lookBy(float yawDelta, float pitchDelta) {
    float newPitch = xRot + pitchDelta;
    // Mesmo clamp do input do jogador real: acima de 90° a câmera viraria de
    // cabeça para baixo e o vetor de visão passaria a apontar para trás.
    newPitch = std::max(-90.0f, std::min(90.0f, newPitch));
    setRot(yRot + yawDelta, newPitch);
}

// --- Mira ---------------------------------------------------------------

BotTarget BotPlayer::pickTarget(double range) {
    BotTarget target;
    if (level == nullptr) return target;

    // Bloco: exatamente o mesmo raycast que o jogo faz para o jogador
    // (GameRenderer::pick -> LivingEntity::pick), inclusive a origem do raio,
    // para o bot mirar no mesmo lugar que um humano miraria.
    HitResult* tileHit = pick(range, 1.0f);

    Vec3 from = getPos(1.0f);
    double tileDist = range;
    if (tileHit != nullptr) tileDist = tileHit->pos.distanceTo(from);

    // Entidade: mesma varredura do GameRenderer::pick — expande a bb do bot
    // ao longo do vetor de visão e clipa cada entidade "pickable" contra o
    // raio, ficando com a mais próxima que esteja à frente do bloco.
    Vec3 dir = getViewVector(1.0f);
    Vec3 to(dir.x * range, dir.y * range, dir.z * range);
    to = to.add(from.x, from.y, from.z);

    const float overlap = 1.0f;
    AABB grown = bb.expand(dir.x * range, dir.y * range, dir.z * range)
                     .grow(overlap, overlap, overlap);

    // ATENÇÃO: getEntities() NÃO transfere posse — devolve um ponteiro para o
    // vetor membro Level::es, reaproveitado (clear()) a cada chamada. Só
    // getEntitiesOfClass() devolve um vetor de heap que o chamador deve
    // deletar. Dar delete aqui corromperia a heap.
    std::vector<std::shared_ptr<Entity>>* nearby =
        level->getEntities(shared_from_this(), &grown);

    std::shared_ptr<Entity> hovered = nullptr;
    double nearest = tileDist;
    if (nearby != nullptr) {
        for (auto& e : *nearby) {
            if (e == nullptr || e.get() == this) continue;
            if (!e->isPickable()) continue;

            float rr = e->getPickRadius();
            AABB entityBox = e->bb.grow(rr, rr, rr);
            if (entityBox.contains(from)) {
                hovered = e;
                nearest = 0.0;
                continue;
            }
            HitResult* p = entityBox.clip(from, to);
            if (p != nullptr) {
                double dd = from.distanceTo(p->pos);
                if (dd < nearest) {
                    hovered = e;
                    nearest = dd;
                }
                delete p;
            }
        }
    }

    if (hovered != nullptr) {
        target.kind = BotTarget::ENTITY;
        target.entity = hovered;
        target.distance = nearest;
        delete tileHit;
        return target;
    }

    if (tileHit != nullptr) {
        target.kind = BotTarget::TILE;
        target.x = tileHit->x;
        target.y = tileHit->y;
        target.z = tileHit->z;
        target.face = tileHit->f;
        // Coordenadas do clique dentro da face, no mesmo formato que
        // Item::useOn espera (0..1 relativo à célula atingida).
        target.clickX = static_cast<float>(tileHit->pos.x - tileHit->x);
        target.clickY = static_cast<float>(tileHit->pos.y - tileHit->y);
        target.clickZ = static_cast<float>(tileHit->pos.z - tileHit->z);
        target.distance = tileDist;
        delete tileHit;
    }
    return target;
}

// --- Cliques ------------------------------------------------------------

bool BotPlayer::attackTarget() {
    BotTarget target = pickTarget();

    if (target.kind == BotTarget::ENTITY) {
        if (!isAllowedToHurtEntity(target.entity)) return false;
        attack(target.entity);
        return true;
    }
    if (target.kind == BotTarget::TILE) {
        return destroyTileAt(target.x, target.y, target.z);
    }
    return false;
}

bool BotPlayer::useTarget() {
    if (level == nullptr) return false;

    BotTarget target = pickTarget();
    std::shared_ptr<Player> self = selfAsPlayer();
    if (self == nullptr) return false;

    // 1) Entidade sob a mira: Player::interact — montar cavalo/porco, tosquiar
    //    ovelha, domesticar lobo, abrir inventário de baú-de-burro, trocar com
    //    aldeão. Tudo já implementado pelo jogo.
    if (target.kind == BotTarget::ENTITY) {
        if (!isAllowedToInteract(target.entity)) return false;
        return interact(target.entity);
    }

    // 2) Bloco sob a mira: mesma ordem de ServerPlayerGameMode::useItemOn —
    //    primeiro Tile::use (porta, botão, alavanca, baú, fornalha, bancada,
    //    cama...), e só se o bloco não consumir o clique é que o item age
    //    (ItemInstance::useOn: colocar bloco, arar terra, acender fogo,
    //    encher balde, plantar muda).
    std::shared_ptr<ItemInstance> held = getSelectedItem();
    if (target.kind == BotTarget::TILE) {
        int t = level->getTile(target.x, target.y, target.z);
        if (!isSneaking() || held == nullptr) {
            if (t > 0 && Tile::tiles[t] != nullptr &&
                isAllowedToUse(Tile::tiles[t])) {
                if (Tile::tiles[t]->use(level, target.x, target.y, target.z,
                                        self, target.face, target.clickX,
                                        target.clickY, target.clickZ)) {
                    return true;
                }
            }
        }

        if (held == nullptr || !isAllowedToUse(held)) return false;
        bool used =
            held->useOn(self, level, target.x, target.y, target.z, target.face,
                        target.clickX, target.clickY, target.clickZ);
        if (held->count == 0) removeSelectedItem();
        return used;
    }

    // 3) Nada sob a mira: uso "no ar" — comer, beber poção, puxar arco,
    //    lançar bola de neve/ovo, jogar vara de pescar. Mesma sequência de
    //    ServerPlayerGameMode::useItem: o item devolve a instância resultante,
    //    que volta para o slot selecionado.
    if (held == nullptr || !isAllowedToUse(held)) return false;

    int oldCount = held->count;
    int oldAux = held->getAuxValue();
    std::shared_ptr<ItemInstance> result = held->use(level, self);
    bool changed =
        result != held ||
        (result != nullptr &&
         (result->count != oldCount || result->getUseDuration() > 0 ||
          result->getAuxValue() != oldAux));
    if (!changed) return false;

    inventory->items[inventory->selected] = result;
    if (result != nullptr && result->count == 0) {
        inventory->items[inventory->selected] = nullptr;
    }
    inventory->setChanged();
    return true;
}

bool BotPlayer::destroyTileAt(int x, int y, int z) {
    if (level == nullptr) return false;
    if (!isAllowedToMine()) return false;
    if (!mayDestroyBlockAt(x, y, z)) return false;

    int t = level->getTile(x, y, z);
    if (t <= 0) return false;  // ar, ou id inválido — nada a quebrar
    Tile* oldTile = Tile::tiles[t];
    if (oldTile == nullptr) return false;

    int data = level->getData(x, y, z);
    std::shared_ptr<Player> self = selfAsPlayer();

    // Sequência de ServerPlayerGameMode::destroyBlock no ramo de
    // sobrevivência (superDestroyBlock + desgaste de ferramenta + drops).
    oldTile->playerWillDestroy(level, x, y, z, data, self);
    bool changed = level->removeTile(x, y, z);
    if (changed) oldTile->destroy(level, x, y, z, data);

    std::shared_ptr<ItemInstance> item = getSelectedItem();
    bool canDrop = canDestroy(oldTile);
    if (item != nullptr) {
        item->mineBlock(level, t, x, y, z, self);
        if (item->count == 0) removeSelectedItem();
    }
    if (changed && canDrop) {
        oldTile->playerDestroy(level, self, x, y, z, data);
    }
    return changed;
}

// --- Atalhos de compatibilidade (Etapa 1) -------------------------------

bool BotPlayer::breakBlockInFront() {
    BotTarget target = pickTarget();
    if (target.kind != BotTarget::TILE) return false;
    return destroyTileAt(target.x, target.y, target.z);
}

bool BotPlayer::placeBlockInFront() {
    if (level == nullptr) return false;
    std::shared_ptr<ItemInstance> held = getSelectedItem();
    if (held == nullptr) return false;

    BotTarget target = pickTarget();
    if (target.kind != BotTarget::TILE) return false;

    std::shared_ptr<Player> self = selfAsPlayer();
    if (self == nullptr) return false;
    if (!isAllowedToUse(held)) return false;

    // Diferente de useTarget(), aqui pulamos Tile::use de propósito: este
    // atalho existe para "colocar bloco" determinístico, sem o risco de o
    // clique ser consumido por um baú/porta atrás da mira.
    bool placed =
        held->useOn(self, level, target.x, target.y, target.z, target.face,
                    target.clickX, target.clickY, target.clickZ);
    if (held->count == 0) removeSelectedItem();
    return placed;
}

bool BotPlayer::attackNearestEntity(double radius) {
    std::shared_ptr<Entity> target = getNearestEntity(radius);
    if (target == nullptr) return false;
    if (!isAllowedToHurtEntity(target)) return false;
    attack(target);
    return true;
}

// --- Inventário ---------------------------------------------------------

bool BotPlayer::selectSlot(int slot) {
    if (inventory == nullptr) return false;
    if (slot < 0 || slot >= Inventory::getSelectionSize()) return false;
    inventory->selected = slot;
    return true;
}

bool BotPlayer::swapInventorySlots(int a, int b) {
    if (inventory == nullptr) return false;
    int size = static_cast<int>(inventory->getContainerSize());
    if (a < 0 || b < 0 || a >= size || b >= size || a == b) return false;
    inventory->swapSlots(a, b);
    return true;
}

bool BotPlayer::dropSelected(bool wholeStack) {
    if (inventory == nullptr) return false;
    if (inventory->getSelected() == nullptr) return false;
    return drop(wholeStack) != nullptr;
}

void BotPlayer::dropWholeInventory() {
    if (inventory != nullptr) inventory->dropAll();
}

// --- Container ----------------------------------------------------------

bool BotPlayer::hasContainerOpen() const {
    return containerMenu != nullptr && containerMenu != inventoryMenu;
}

void BotPlayer::closeContainerMenu() {
    if (!hasContainerOpen()) return;
    std::shared_ptr<Player> self = selfAsPlayer();
    if (self != nullptr) containerMenu->removed(self);
    // Não damos delete no menu: Player::closeContainer() (o caminho vanilla)
    // também só reaponta containerMenu para inventoryMenu. Quem construiu o
    // menu — Tile::use, via openContainer/openFurnace/... — é quem manda no
    // ciclo de vida dele.
    closeContainer();
}

// --- Leitura de estado --------------------------------------------------

int BotPlayer::getTileAtOffset(int dx, int dy, int dz) const {
    if (level == nullptr) return -1;
    int bx = static_cast<int>(std::floor(x)) + dx;
    int by = static_cast<int>(std::floor(y)) + dy;
    int bz = static_cast<int>(std::floor(z)) + dz;
    return level->getTile(bx, by, bz);
}

int BotPlayer::getBlockIdInFront() {
    BotTarget target = pickTarget();
    if (target.kind != BotTarget::TILE) return 0;  // nada sólido na mira = ar
    if (level == nullptr) return -1;
    return level->getTile(target.x, target.y, target.z);
}

std::shared_ptr<Entity> BotPlayer::getNearestEntity(double radius) {
    if (level == nullptr) return nullptr;

    AABB box(x - radius, y - radius, z - radius, x + radius, y + radius,
             z + radius);
    // Ver nota de posse em pickTarget(): o vetor devolvido é do Level.
    std::vector<std::shared_ptr<Entity>>* nearby =
        level->getEntities(shared_from_this(), &box);
    if (nearby == nullptr) return nullptr;

    std::shared_ptr<Entity> best = nullptr;
    double bestDist = radius * radius;
    for (auto& e : *nearby) {
        if (e == nullptr || e.get() == this) continue;
        if (!e->isAlive()) continue;
        double dx = e->x - x, dy = e->y - y, dz = e->z - z;
        double dist = dx * dx + dy * dy + dz * dz;
        if (dist < bestDist) {
            bestDist = dist;
            best = e;
        }
    }
    return best;
}

// --- Coordenação com a "Minecraft Server thread" ---------------------------

namespace {
std::mutex g_curiousMobSpawnMutex;
bool g_curiousMobSpawnRequested = false;
int g_curiousMobSpawnDimension = 0;
double g_curiousMobSpawnX = 0, g_curiousMobSpawnY = 0, g_curiousMobSpawnZ = 0;
std::shared_ptr<BotPlayer> g_curiousMobBot;
}  // namespace

void CuriousMobRequestSpawn(int dimension, double x, double y, double z) {
    std::lock_guard<std::mutex> lock(g_curiousMobSpawnMutex);
    if (g_curiousMobBot != nullptr) return;  // já spawnado.
    g_curiousMobSpawnRequested = true;
    g_curiousMobSpawnDimension = dimension;
    g_curiousMobSpawnX = x;
    g_curiousMobSpawnY = y;
    g_curiousMobSpawnZ = z;
}

// Chamado de dentro de MinecraftServer::tick() - já estamos na thread do
// servidor aqui, então é seguro construir o BotPlayer contra a ServerLevel e
// chamar addEntity().
void CuriousMobTickPendingSpawn(MinecraftServer* server) {
    bool requested;
    int dimension;
    double x, y, z;
    {
        std::lock_guard<std::mutex> lock(g_curiousMobSpawnMutex);
        requested = g_curiousMobSpawnRequested;
        if (!requested) return;
        g_curiousMobSpawnRequested = false;
        dimension = g_curiousMobSpawnDimension;
        x = g_curiousMobSpawnX;
        y = g_curiousMobSpawnY;
        z = g_curiousMobSpawnZ;
    }

    ServerLevel* level = server->getLevel(dimension);
    if (level == nullptr) {
        fprintf(stderr,
                "[CuriousMob] spawn abortado: ServerLevel nula para "
                "dimension=%d\n",
                dimension);
        return;
    }

    std::shared_ptr<BotPlayer> bot =
        std::shared_ptr<BotPlayer>(new BotPlayer(level, L"CuriousMob"));
    bot->moveTo(x, y, z, 0, 0);
    bool addedOk = level->addEntity(bot);

    // Level::addEntity() empurra automaticamente qualquer entidade
    // instanceof(eTYPE_PLAYER) para level->players - só que boa parte do
    // código que itera esse vetor assume que todo mundo lá é um
    // ServerPlayer de rede de verdade (dynamic_pointer_cast<ServerPlayer>
    // sem checar null antes de usar, ex.: EntityTracker::tick() faz
    // `player->isAlive()` direto no resultado do cast). Pra um BotPlayer
    // isso vira um shared_ptr nulo e derruba o jogo no tick seguinte do
    // servidor. O bot continua sendo uma entidade normal do mundo (chunks/
    // entities, onde a IA dos mobs realmente procura alvo) - só não pode
    // fingir ser um "jogador conectado" nesse vetor específico.
    if (addedOk) {
        auto& levelPlayers = level->players;
        levelPlayers.erase(
            std::remove(levelPlayers.begin(), levelPlayers.end(), bot),
            levelPlayers.end());
    }

    fprintf(stderr,
            "[CuriousMob] spawn (thread do servidor): bot=(%.2f,%.2f,%.2f) "
            "addEntity=%s\n",
            bot->x, bot->y, bot->z, addedOk ? "ok" : "FALHOU");

    std::lock_guard<std::mutex> lock(g_curiousMobSpawnMutex);
    g_curiousMobBot = bot;
}

std::shared_ptr<BotPlayer> CuriousMobGetBot() {
    std::lock_guard<std::mutex> lock(g_curiousMobSpawnMutex);
    return g_curiousMobBot;
}
