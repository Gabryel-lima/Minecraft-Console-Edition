#pragma once

// CuriousMob — código do mod, isolado do jogo vanilla.
// Ver mods/CuriousMob/PLANO.md e mods/CuriousMob/README.md na raiz do repo.
//
// BotPlayer é um jogador headless: estende Player (não Mob/Animal) para ter
// paridade de mecânicas com um jogador real (inventário, attack(), etc.),
// mas sem PlayerConnection/Input/Minecraft* — é dirigido pelo
// CuriousMobController a cada tick, que por sua vez lê ações vindas da
// ponte TCP (CuriousMobBridge) com o processo Python externo.
//
// PARIDADE COM O PLAYER
// ---------------------
// A superfície de ações abaixo espelha o que um jogador humano pode fazer:
//
//   input     -> setMoveInput / lookBy / setSneaking / setSprinting
//   mira      -> pickTarget()   (raycast, igual ao GameRenderer::pick)
//   ataque    -> attackTarget() (entidade: Player::attack; bloco: destruir)
//   uso       -> useTarget()    (entidade: Player::interact; bloco:
//                                Tile::use e depois ItemInstance::useOn;
//                                ar: ItemInstance::use)
//   item      -> releaseUsingItem / stopUsingItem (arco, comida)
//   inventário-> selectSlot / swapSlots / dropSelected / dropAll
//   container -> closeContainerMenu (a abertura acontece via useTarget,
//                que é o mesmo caminho do jogador real)
//
// O ponto importante é que attackTarget()/useTarget() NÃO são ações novas
// inventadas para o bot: são os dois cliques do mouse do jogador, roteados
// pelo mesmo `if entidade / senão bloco / senão ar` que
// ServerPlayerGameMode::useItemOn e GameMode::attack usam. Qualquer mecânica
// que o jogo implemente dentro de Tile::use ou Item::useOn (portas, botões,
// baús, fornalhas, baldes, isqueiro, enxada, plantar, tosquiar, montar,
// domesticar...) passa a valer para o bot de graça, sem código específico
// aqui.
//
// Quebrar bloco não usa ServerPlayerGameMode (acoplado a um Minecraft*/
// ServerPlayer vivos, com timing de inicialização incerto para um bot). Em
// vez disso, replicamos a sequência de sobrevivência dele em destroyTileAt():
// playerWillDestroy -> removeTile -> destroy -> mineBlock (desgaste da
// ferramenta) -> playerDestroy (drops/XP).

#include <memory>

#include "../../../Minecraft.World/Player/Player.h"

class Level;
class Entity;
class CuriousMobController;
class MinecraftServer;

// Resultado da mira do bot: o análogo do `Minecraft::hitResult` do jogador.
struct BotTarget {
    enum Kind { NONE, TILE, ENTITY };

    Kind kind = NONE;

    // Válidos quando kind == TILE.
    int x = 0, y = 0, z = 0;
    int face = 0;
    float clickX = 0.5f, clickY = 0.5f, clickZ = 0.5f;

    // Válido quando kind == ENTITY.
    std::shared_ptr<Entity> entity;

    double distance = 0.0;
};

class BotPlayer : public Player {
public:
    // Alcance de interação, em blocos — o mesmo do jogador em sobrevivência
    // (GameMode::getPickRange).
    static constexpr double REACH = 4.5;

    BotPlayer(Level* level, const std::wstring& name);
    virtual ~BotPlayer();

    virtual void tick();

    // Player usa heightOffset = 1.62 (o `y` é a altura dos olhos), mas todo o
    // lado servidor/rede assume a convenção do ServerPlayer: heightOffset = 0,
    // com `y` na altura dos pés. O RemotePlayer (espelho client-side que
    // desenha o bot na tela) também assume 0, então herdar o 1.62 do Player
    // fazia o cliente renderizar o bot 1,62 bloco acima do chão ("voando") e
    // os mobs calcularem distanceToSqr() até um ponto alto demais. Espelhamos
    // ServerPlayer::setDefaultHeadHeight() - é preciso ser override, e não só
    // uma atribuição no construtor, porque Player::die() muda o heightOffset
    // para 0.1 e o respawn o restaura chamando este método.
    virtual void setDefaultHeadHeight() { heightOffset = 0; }

    // CommandSender: o bot nunca recebe EGameCommand pela ponte, então não
    // precisa de permissão para nenhum.
    virtual bool hasPermission(EGameCommand command) { return false; }

    // --- Input (espelha o que o Input real escreve num LocalPlayer) ------
    void setMoveInput(float strafe, float forward, bool jump);
    void lookBy(float yawDelta, float pitchDelta);
    void turnBy(float yawDelta) { lookBy(yawDelta, 0.0f); }

    // --- Mira ------------------------------------------------------------
    // Raycast a partir dos olhos, entidade tem prioridade sobre bloco quando
    // está mais perto — mesma regra do GameRenderer::pick.
    BotTarget pickTarget(double range = REACH);

    // --- Cliques ---------------------------------------------------------
    bool attackTarget();  // botão esquerdo
    bool useTarget();     // botão direito

    // Atalhos que o protocolo ainda expõe (compat. com a Etapa 1): agem
    // sobre o alvo mirado, não sobre uma célula fixa à frente.
    bool breakBlockInFront();
    bool placeBlockInFront();
    bool attackNearestEntity(double radius);

    // --- Item em uso (arco carregando, comida sendo comida) --------------
    void releaseItem() { releaseUsingItem(); }
    void cancelItem() { stopUsingItem(); }

    // --- Inventário ------------------------------------------------------
    bool selectSlot(int slot);            // hotbar, 0..8
    bool swapInventorySlots(int a, int b);
    bool dropSelected(bool wholeStack);
    void dropWholeInventory();

    // --- Container -------------------------------------------------------
    bool hasContainerOpen() const;
    void closeContainerMenu();

    // --- Leitura de estado (usada pelo controller p/ montar o JSON) ------
    int getBlockIdInFront();
    int getTileAtOffset(int dx, int dy, int dz) const;
    // Não é const: precisa de shared_from_this() para se excluir da busca,
    // e a versão const disso devolve shared_ptr<const Entity>, que
    // Level::getEntities não aceita.
    std::shared_ptr<Entity> getNearestEntity(double radius);

private:
    CuriousMobController* controller;

    // Sequência completa de destruição de bloco em sobrevivência.
    bool destroyTileAt(int x, int y, int z);

    // Chamado do tick() quando a vida chega a 0: repete o que o cliente faz
    // ao clicar em "Respawn" na tela de morte (PlayerConnection::
    // handleClientCommand -> PlayerList::respawn), mas sem a parte de rede
    // e sem recriar a entidade — o bot não tem PlayerConnection, então
    // fazemos a versão mínima: vida/fome/fogo/hitbox de volta ao normal e
    // teleporte pra cama (se ainda válida) ou pro spawn do mundo.
    void respawnAfterDeath();

    std::shared_ptr<Player> selfAsPlayer();
};

// --- Coordenação com a "Minecraft Server thread" ----------------------------
//
// A ServerLevel (onde o bot precisa viver para ser visto por mobs e sofrer
// dano de verdade) só pode ser mexida a partir da thread do servidor: é ela
// quem tickeia a ServerLevel e itera as listas de entidade continuamente.
// Minecraft::tick() roda na thread do cliente, então construir o BotPlayer e
// chamar ServerLevel::addEntity() direto de lá corrompe essas listas (o
// servidor pode estar no meio de uma iteração/tick ao mesmo tempo) e derruba
// o jogo.
//
// Por isso o spawn vira um pedido: CuriousMobRequestSpawn() (thread do
// cliente) só grava a posição desejada atrás de um mutex; quem de fato cria
// e adiciona o BotPlayer é CuriousMobTickPendingSpawn() (thread do
// servidor), chamado de dentro de MinecraftServer::tick(). O ponteiro do bot
// resultante fica protegido pelo mesmo mutex e só é exposto de volta pra
// thread do cliente via CuriousMobGetBot() (cópia do shared_ptr, não
// acesso direto aos campos do bot).
void CuriousMobRequestSpawn(int dimension, double x, double y, double z);
void CuriousMobTickPendingSpawn(MinecraftServer* server);
std::shared_ptr<BotPlayer> CuriousMobGetBot();
