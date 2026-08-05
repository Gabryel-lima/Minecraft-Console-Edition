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
// Quebrar/colocar bloco NÃO usa GameMode nem ServerPlayerGameMode (ambos
// acoplados a um Minecraft*/ServerPlayer vivos, com timing de inicialização
// incerto para um bot). Em vez disso, chama diretamente as primitivas de
// Level/Tile — o mesmo nível que GameMode::destroyBlock/TileItem::useOn
// usam por baixo.

#include "../../../Minecraft.World/Player/Player.h"

class Level;
class CuriousMobController;

class BotPlayer : public Player {
public:
    BotPlayer(Level* level, const std::wstring& name);
    virtual ~BotPlayer();

    virtual void tick();

    // CommandSender: o bot nunca recebe EGameCommand pela ponte, então não
    // precisa de permissão para nenhum.
    virtual bool hasPermission(EGameCommand command) { return false; }

    // Input de movimento para este tick — espelha o que o Input real
    // escreveria para um LocalPlayer antes de Player::aiStep() rodar.
    void setMoveInput(float strafe, float forward, bool jump);
    void turnBy(float yawDelta);

    // Ações de mundo, autocontidas.
    bool breakBlockInFront();
    bool placeBlockInFront();
    bool attackNearestEntity(double radius);

    // Id do bloco (tile) na célula à frente do bot, ou -1 se indisponível.
    int getBlockIdInFront();

private:
    CuriousMobController* controller;

    bool getBlockInFrontPos(int& outX, int& outY, int& outZ);
};
