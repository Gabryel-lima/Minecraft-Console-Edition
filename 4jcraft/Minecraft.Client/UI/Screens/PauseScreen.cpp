#include "PauseScreen.h"
#include "TitleScreen.h"
#include "MessageScreen.h"
#include "OptionsScreen.h"
#include "../Button.h"
#include "../../MinecraftServer.h"
#include "../../Platform/stdafx.h"
#include "../../Player/LocalPlayer.h"
#include "../../GameState/StatsCounter.h"
#include "../../Level/MultiPlayerLevel.h"
#include "../../Player/MultiPlayerLocalPlayer.h"
#include "../../../Minecraft.World/Headers/net.minecraft.stats.h"
#include "../../../Minecraft.World/Headers/net.minecraft.locale.h"
#include "../../../Minecraft.World/Headers/net.minecraft.world.level.h"

PauseScreen::PauseScreen() {
    saveStep = 0;
    visibleTime = 0;
}

void PauseScreen::init() {
    saveStep = 0;
    buttons.clear();
    const int buttonX = width / 2 - 100;
    const int menuTop = height / 2 - 48;
    // 4jcraft: solves the issue of client-side only pausing in the java gui
    if (g_NetworkManager.IsLocalGame() &&
        g_NetworkManager.GetPlayerCount() == 1)
        app.SetXuiServerAction(ProfileManager.GetPrimaryPad(),
                               eXuiServerAction_PauseServer, (void*)true);
    buttons.push_back(new Button(4, buttonX, menuTop,
                                 I18n::get(L"menu.returnToGame")));
    buttons.push_back(new Button(0, buttonX, menuTop + 24,
                                 I18n::get(L"menu.options")));
    buttons.push_back(new Button(5, buttonX, menuTop + 48, 98, 20,
                                 I18n::get(L"gui.achievements")));
    buttons.push_back(new Button(6, buttonX + 102, menuTop + 48, 98, 20,
                                 I18n::get(L"gui.stats")));

    Button* returnToMenuButton =
        new Button(1, buttonX, menuTop + 72, I18n::get(L"menu.returnToMenu"));
    if (!g_NetworkManager.IsHost()) {
        returnToMenuButton->msg = I18n::get(L"menu.disconnect");
    }
    buttons.push_back(returnToMenuButton);
    /*
     * if (minecraft->serverConnection!=null) { buttons.get(1).active =
     * false; buttons.get(2).active = false; buttons.get(3).active = false;
     * }
     */
}

void PauseScreen::exitWorld(Minecraft* minecraft, bool save) {
    // 4jcraft: made our own static method for use in the java gui (other
    // places such as the deathscreen need this)
    MinecraftServer* server = MinecraftServer::getInstance();

    minecraft->setScreen(new MessageScreen(L"Leaving world"));
    if (g_NetworkManager.IsHost()) {
        server->setSaveOnExit(save);
    }
    app.SetAction(minecraft->player->GetXboxPad(), eAppAction_ExitWorld);
}

void PauseScreen::buttonClicked(Button* button) {
    if (button->id == 0) {
        minecraft->setScreen(new OptionsScreen(this, minecraft->options));
    }
    if (button->id == 1) {
        // if (minecraft->isClientSide())
        // {
        //     minecraft->level->disconnect();
        // }

        // minecraft->setLevel(nullptr);
        // minecraft->setScreen(new TitleScreen());

        // 4jcraft: exit with our new exitWorld method
        exitWorld(minecraft, true);
    }
    if (button->id == 4) {
        app.SetXuiServerAction(ProfileManager.GetPrimaryPad(),
                               eXuiServerAction_PauseServer, (void*)false);
        minecraft->setScreen(nullptr);
        //       minecraft->grabMouse();		// 4J - removed
    }

    if (button->id == 5) {
        //        minecraft->setScreen(new AchievementScreen(minecraft->stats));
        //        // 4J TODO - put back
    }
    if (button->id == 6) {
        //        minecraft->setScreen(new StatsScreen(this, minecraft->stats));
        //        // 4J TODO - put back
    }
}

void PauseScreen::tick() {
    Screen::tick();
    visibleTime++;
}

void PauseScreen::render(int xm, int ym, float a) {
    const int menuTop = height / 2 - 48;
    renderBackground();

    bool isSaving = false;  //! minecraft->level->pauseSave(saveStep++);
    if (isSaving || visibleTime < 20) {
        float col = ((visibleTime % 10) + a) / 10.0f;
        col = Mth::sin(col * PI * 2) * 0.2f + 0.8f;
        int br = (int)(255 * col);

        drawCenteredString(font, L"Saving level..", width / 2, height - 16,
                           br << 16 | br << 8 | br);
    }

    drawCenteredString(font, L"Game menu", width / 2, menuTop - 32, 0xffffff);

    Screen::render(xm, ym, a);
}
