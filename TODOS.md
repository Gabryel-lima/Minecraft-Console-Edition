 CORRIGIDO: o BotPlayer era adicionado só à MultiPlayerLevel (espelho client-side), por isso
     a IA dos mobs (que itera a ServerLevel) nunca o via, e o dano real (resolvido server-side)
     nunca chegava nele. Agora ele é criado e adicionado na ServerLevel de verdade (Minecraft.cpp,
     spawn do CuriousMob), então mobs o enxergam e attackEntityFrom/die rodam normalmente (inclui
     dropAll() do inventário, ou seja, perde os itens de verdade). Também foi adicionado
     BotPlayer::respawnAfterDeath() (chamado do tick() quando health<=0), que reproduz o que o
     cliente faz ao clicar em "Respawn" (vida/fome/fogo/hitbox resetados, teleporte pra cama
     válida ou pro spawn do mundo), já que o caminho normal (PlayerList::respawn) é amarrado a
     ServerPlayer+PlayerConnection e o bot não tem nenhum dos dois. O respawn também zera XP
     (experienceLevel/totalExperience/experienceProgress) quando a regra keepInventory está
     desligada, espelhando Player::restoreFrom - senão o XP sobreviveria à morte sem querer.
     Ainda não testado dentro do jogo (só validado que compila) - falta rodar uma sessão real
     com o bot levando dano de um mob até morrer e confirmar respawn/perda de itens+xp no mundo.

 CRASH 1 (corrigido): ServerLevel::addEntity(bot) era chamado direto de Minecraft::tick()
     (thread do cliente), mas só a "Minecraft Server thread" pode mexer na ServerLevel - corrida
     de dados corrompendo as listas de entidade. Virou um pedido assíncrono: Minecraft.cpp chama
     CuriousMobRequestSpawn() (só grava a posição atrás de um mutex), e quem cria o BotPlayer de
     verdade e chama addEntity() é CuriousMobTickPendingSpawn(), chamado de dentro de
     MinecraftServer::tick() (MinecraftServer.cpp) - já na thread certa.

 CRASH 2 (corrigido): Level::addEntity() empurra qualquer instanceof(eTYPE_PLAYER) pro vetor
     Level::players, e EntityTracker::tick() (EntityTracker.cpp:195-201) itera esse vetor fazendo
     dynamic_pointer_cast<ServerPlayer> sem checar null antes de chamar player->isAlive() - pro
     BotPlayer isso é sempre nulo, crash garantido no tick seguinte do servidor. Corrigido
     removendo o bot de level->players logo após o addEntity (BotPlayer.cpp,
     CuriousMobTickPendingSpawn), mantendo ele nas listas de chunk/entidades (é lá que a IA dos
     mobs busca alvo, então a visibilidade pros mobs não é afetada).
     
 CRASH 3 (corrigido, era o que realmente derrubava o jogo ao andar): Entity::playStepSound
     (som de passo) chama Player::playSound -> Level::playPlayerSound ->
     ServerLevelListener::playSoundExceptPlayer -> PlayerList::broadcast(except, ...)
     (PlayerList.cpp:1291), que fazia dynamic_pointer_cast<ServerPlayer>(except) sem checar null e
     colocava o resultado em sentTo - pro BotPlayer (que não é ServerPlayer) isso é nullptr, e o
     loop seguinte desreferencia player2->connection desse ponteiro nulo. Corrigido só inserindo em
     sentTo quando o cast realmente resulta num ServerPlayer válido.
     CORRIGIDO: EntityTracker::addEntity(shared_ptr<Entity>) (EntityTracker.cpp:28) só tratava
     eTYPE_SERVERPLAYER e uma lista fixa de outros tipos - sem branch genérica pra eTYPE_PLAYER, o
     BotPlayer nunca virava um TrackedEntity e nunca era enviado por pacote de spawn pro cliente do
     jogador humano (ficava invisível na tela, mesmo já sendo atacável/visível pra IA dos mobs, que
     usa as listas de chunk/entidade e não o EntityTracker). Adicionado um branch pra eTYPE_PLAYER
     que registra o bot com o mesmo alcance/intervalo do ServerPlayer (32*16, 2), mas sem o loop de
     "avisar entidades já trackadas" (isso é só pra sincronizar estado inicial de um cliente de
     rede recém-conectado, e o bot não tem conexão). Também foi preciso ensinar
     TrackedEntity::getAddEntityPacket() (TrackedEntity.cpp:695) a montar um AddPlayerPacket pra
     Player genérico (não só ServerPlayer) - o construtor do pacote só usa métodos genéricos de
     Player (nome, inventário, skin/cape), então funciona igual, só sem XUID de conta online.
     Ainda não testado dentro do jogo se o bot agora aparece visualmente pro jogador humano - falta
     confirmar numa sessão real.

 CRASH 4 (corrigido): consequência direta do fix do EntityTracker acima. Level::addEntity()
     (Level.cpp:1600-1611, código do motor) põe a entidade em level->players ANTES de chamar
     entityAdded() - e é entityAdded() que dispara EntityTracker::addEntity(), que agora (por causa
     do fix de visibilidade) chama TrackedEntity::updatePlayers(&level->players) já durante o
     registro do próprio bot. Nesse instante o bot ainda está dentro de level->players (nosso erase
     só roda depois que addEntity() retorna, tarde demais pra esse caminho síncrono), então
     dynamic_pointer_cast<ServerPlayer> falha pra ele e TrackedEntity::updatePlayer() recebia um
     sp nulo, derrubando o jogo em isVisible() (TrackedEntity.cpp:465, sp->x). Corrigido com um
     "if (sp == nullptr) return;" logo no início de updatePlayer() (TrackedEntity.cpp) - único
     ponto por onde esse ponteiro passa antes de ser desreferenciado.

 BOT "VOANDO" (corrigido): o BotPlayer herdava heightOffset = 1.62 do construtor de Player, ou
     seja, seu `y` era a altura dos OLHOS. Mas a convenção do lado servidor/rede é a do
     ServerPlayer, que faz heightOffset = 0 (ServerPlayer.cpp:118, com comentário explicando que
     precisa ser setado ANTES do moveTo) e sobrescreve setDefaultHeadHeight() para 0
     (ServerPlayer.cpp:225) - `y` na altura dos PÉS. O RemotePlayer (espelho client-side que
     desenha o bot na tela) também assume 0 (RemotePlayer.cpp:16). Resultado: o servidor mandava
     y=72.62 (pés em 71.0 + 1.62), o cliente lia como pés e desenhava o bot 1,62 bloco no ar.
     Confirmado pelos logs de diagnóstico: onGround=1 e y=72.62 estável, ou seja, server-side o
     bot estava corretamente no chão o tempo todo - o "voo" era só renderização.
     Corrigido em BotPlayer: heightOffset = 0 no construtor + override de setDefaultHeadHeight()
     (o override é necessário porque Player::die() muda o offset pra 0.1 e o respawn o restaura
     chamando esse método).
     EFEITO COLATERAL ESPERADO no dano: Entity::distanceToSqr() usa x,y,z crus, então os mobs
     também calculavam a posição do bot 1,62 alto demais, atrapalhando o alcance de ataque deles.

 BOT "NÃO MORRE": pelos logs, NÃO é bug - o dano aplica normalmente (health foi de 20.00 até
     8.50 levando hits) e depois subiu 8.50 -> 9.50, ou seja, regeneração natural com fome cheia,
     que é exatamente o comportamento de sobrevivência correto. O bot não morria porque vagava pra
     longe do mob e se curava antes de acumular dano suficiente. foodData.tick() roda normalmente
     (Player.cpp:326, só no lado servidor) e causeFoodExhaustion não é ignorado (o bot não tem o
     privilégio ClassicHunger). Reavaliar depois do fix de heightOffset, já que os mobs devem
     passar a acertar com mais consistência.

 LOGS POLUÍDOS (corrigido): OutputDebugStringA (Platform/Linux/Stubs/winapi_stubs.h) tinha um
     "limitador de spam" (commit 7229996) que emitia um clear-screen ANSI \x1b[2J\x1b[H a cada 50
     caracteres impressos. Como uma linha de log quase sempre passa de 50 caracteres, isso limpava
     a tela a CADA linha - destruindo o histórico e enchendo o texto copiado de sequências de
     escape (que aparecem como linhas em branco/espaços). O bloco estava ativo porque meson.build
     define -D_DEBUG e -D_DEBUG_MENUS_ENABLED no buildtype padrão. Removido: agora só faz fputs().
     Bônus de desempenho: eram várias syscalls extras por linha de log.

 MUNDO NÃO SALVA AO SAIR (diagnosticado, NÃO corrigido - é subsistema não implementado):
     O botão "return to menu" está correto - PauseScreen::exitWorld() (PauseScreen.cpp:53-63) chama
     setSaveOnExit(true), e todo o caminho do motor roda: eAppAction_ExitWorld ->
     eAppAction_ExitWorldCapturedThumbnail -> IUIScene_PauseMenu::_ExitWorld ->
     MinecraftServer::HaltServer -> stopServer() -> players->saveAll() + saveAllChunks() +
     saveGameRules() + levels[0]->saveToDisc(). Os gates todos passam (IsSignedIn(0)=true,
     GetSaveDisabled()=false, m_saveOnExit=true, didInit=true), e o gate de trial IsFullVersion()
     já tinha sido removido antes.
     O problema é o FIM da cadeia: toda a camada de armazenamento do console (C4JStorage, alias
     StorageManager) é STUB VAZIO neste port Linux, em Platform/Extrax64Stubs.cpp:557+ —
     SaveSaveData() tem corpo {} (descarta o blob), GetSavesInfo() devolve ESaveGame_Idle sem
     listar nada, LoadSaveData()/GetSaveSize()/GetSaveData()/DoesSaveExist() idem. Confirmado no
     disco: não existe nenhum level.dat nem diretório de save em lugar nenhum.
     Ou seja: não é bug de um botão, é persistência de mundo inteira por implementar (gravar o
     blob+thumbnail em arquivo, enumerar saves pro UIScene_LoadMenu, ler de volta, apagar).
     IMPLEMENTADO em 4jcraft/4J.Storage/4J_Storage.cpp (NÃO em Extrax64Stubs.cpp: o bloco de
     storage de lá tem assinaturas de um header antigo e é código morto sob _WINDOWS64 - conferido
     com nm no binário, que linka as assinaturas com callback do 4J_Storage.cpp).
     Layout em disco, relativo ao cwd (mesma convenção de Common/, music/, Sound/):
       saves/<id>/save.dat        blob principal (buffer de AllocateSaveData)
       saves/<id>/title.txt       nome de exibição em UTF-8 (usado pelo menu de carregar)
       saves/<id>/region_<N>.bin  subfiles de região (SPLIT_SAVES)
     Detalhes que importam:
     - A API é assíncrona no console; aqui o I/O é síncrono e o callback é chamado na hora,
       mantendo os MESMOS valores de retorno dos stubs (ESaveGame_Idle) pra não mudar o fluxo de
       controle de quem chama. Chamar o callback de SaveSaveData é obrigatório: é ele que encadeia
       SaveSubfiles (ConsoleSaveFileSplit::SaveSaveDataCallback).
     - SaveSaveData grava o buffer inteiro alocado porque a API não informa quantos bytes foram
       realmente usados; o formato é auto-delimitado ([versão][tamanho descomprimido][zlib]), então
       sobra no fim é ignorada na leitura. Custa disco, não corretude.
     - GetSubfileDetails entrega memória de malloc() porque quem chama assume a posse e libera com
       free() (RegionFileReference::ReleaseCompressed). UpdateSubfile copia, porque o buffer
       comprimido do chamador é reaproveitado antes de SaveSubfiles rodar.
     - ResetSaveData() zerar o id é o que faz "mundo novo" criar um save novo em vez de
       sobrescrever o carregado; LoadSaveData fixa o id pra saves seguintes sobrescreverem.
     COMPILA E LINKA, mas NÃO foi testado em jogo - falta criar um mundo, sair pelo menu, e
     confirmar que ele reaparece na lista e carrega.

 OTIMIZAÇÃO (avaliação inicial): o build padrão é buildtype=debugoptimized COM -D_DEBUG, -DDEBUG
     e -D_DEBUG_MENUS_ENABLED (meson.build:30-35) - ou seja, asserts ligados e caminhos de debug
     ativos. Existe caminho de release pronto (make build BUILDTYPE=release, que com
     b_ndebug=if-release desliga os asserts e não define _DEBUG). Medir isso antes de qualquer
     refatoração é o passo com melhor relação custo/benefício. Otimização além disso precisa de
     profiling real, não de chute.
