# CuriousMob

Agente jogável controlado por um processo Python externo via uma ponte TCP,
com paridade de ações com o `Player` real (movimento, quebrar/colocar bloco,
atacar). Ver `PLANO.md` para a visão completa e `protocol/messages.md` para o
protocolo de comunicação.

## Estrutura

```
mods/CuriousMob/
  PLANO.md            # plano completo (roadmap, etapas futuras)
  ai/                 # cliente Python (environment.py, policy.py, ...)
  protocol/           # spec do protocolo + (de)serialização Python
  docs/ models/ datasets/ tests/   # placeholders para etapas futuras

4jcraft/Minecraft.Client/Mods/CuriousMob/   # código C++ do bot
  BotPlayer.{h,cpp}            # a entidade (extends Player)
  CuriousMobBridge.{h,cpp}     # servidor TCP local
  CuriousMobController.{h,cpp} # aplica ações da ponte / fallback aleatório
```

## Compilar

Na raiz do repo:

```sh
make build
```

Se você acabou de adicionar/mover arquivos `.cpp`/`.h` sob `4jcraft/` e o
link falhar com "undefined symbol", rode `make reconfigure` antes de `make
build` — o Meson só re-escaneia os fontes (`find` recursivo) na
configuração, não a cada compilação.

## Rodar e spawnar o bot

O bot só é spawnado quando a variável de ambiente `CURIOUSMOB_SPAWN` está
definida (qualquer valor não vazio) — isso evita qualquer efeito colateral
para quem só quer jogar normalmente. O spawn acontece automaticamente perto
do jogador local, assim que o mundo carrega.

```sh
CURIOUSMOB_SPAWN=1 4jcraft/build/Minecraft.Client/Minecraft.Client
```

(ou `CURIOUSMOB_SPAWN=1 make run`, se preferir usar o alvo do Makefile).

Você deve ver um segundo "jogador" (visual padrão de player) aparecer a
poucos blocos de distância de onde você spawnou.

O bot sempre nasce em modo sobrevivência (`abilities` setado explicitamente
em `BotPlayer::BotPlayer`), mesmo que o mundo tenha sido criado/aberto em
modo criativo — o modo de jogo do mundo não é herdado pelo bot porque ele
nunca passa por `GameMode::initPlayer`.

### Teleportar até o bot (F4)

Com o bot spawnado, aperte **F4** a qualquer momento (fora de menus) para
teleportar o jogador local até a posição atual do bot. Útil para achá-lo
depois que ele andou/quebrou blocos e saiu do seu campo de visão. Não usamos
a tecla `F` simples porque ela já é a tecla de interagir/usar do jogo (ver
`4J.Input/4J_Input.cpp`).

## Testar a ponte com o Python

1. (Uma vez) crie um venv e instale as dependências:
   ```sh
   python3 -m venv mods/CuriousMob/ai/.venv
   source mods/CuriousMob/ai/.venv/bin/activate
   pip install -r mods/CuriousMob/ai/requirements.txt
   ```
2. Com o jogo já rodando (`CURIOUSMOB_SPAWN=1 ...`), em outro terminal:
   ```sh
   source mods/CuriousMob/ai/.venv/bin/activate
   python mods/CuriousMob/ai/environment.py
   ```
3. O terminal do Python deve imprimir uma linha por estado recebido
   (`tick=... pos=(...) -> Action(...)`), e o bot no jogo deve passar a se
   mover de forma visivelmente diferente do "andar aleatório" padrão (a
   política em `environment.py` também é aleatória por enquanto, mas os
   valores — incluindo quebra de bloco ocasional — vêm agora do Python, não
   do fallback em C++).

## Status de verificação

Já testado de ponta a ponta (build real + jogo rodando + `environment.py`
conectado): o bot é construído, a ponte escuta em `127.0.0.1:5555`, o
round-trip completo funciona (estado enviado a cada 5 ticks, ação lida de
volta) e o **movimento realmente altera a posição do bot no mundo**
tick a tick, seguindo as ações recebidas do Python — inclusive
`break_block` foi disparado nesse teste sem causar crash. Não foi validado
manualmente ainda (dependem de setup específico no mundo): colocar bloco com
item selecionado e atacar uma entidade próxima — seguem o roteiro abaixo.

## Roteiro de verificação manual (Etapa 1 do PLANO.md)

Sem o Python conectado:
- [ ] O bot aparece no mundo e anda sozinho (fallback aleatório em
      `CuriousMobController::applyRandomWander`).

Com o Python conectado (`environment.py` rodando):
- [ ] O jogo loga/mostra que uma conexão foi aceita (a política aleatória do
      Python começa a controlar o bot).
- [ ] Movimento: o bot anda para frente/trás/esquerda/direita e vira,
      conforme os campos `move`/`turn` da ação.
- [ ] Pulo: em algum momento o bot pula (`jump: true`).
- [ ] Quebrar bloco: aponte o bot (ajustando `yaw`/posição) para um bloco
      simples (terra, pedra) e confirme que uma ação com `break_block: true`
      realmente remove o bloco do mundo.
- [ ] Colocar bloco: dê ao bot um item de bloco no inventário (ex. via
      comando de give a partir de outra ferramenta de teste, já que o bot
      ainda não abre inventário) e confirme que `place_block: true` cria um
      bloco na célula à frente.
- [ ] Atacar: posicione uma entidade hostil perto do bot e confirme que
      `attack: true` causa dano (`Player::attack`) na entidade mais próxima.

## Problemas encontrados e corrigidos

- **Bot invisível/longe do jogador.** O spawn rodava dentro de
  `Minecraft::setLevel()`, ponto em que `player->x/y/z` ainda é a posição
  placeholder pré-rede (ex. `(0.5, ~2.6, 0.5)`), não a posição final que o
  servidor atribui ao jogador via `LoginPacket` (que só chega depois). O bot
  spawnava a centenas de blocos de distância. Corrigido adiando o spawn para
  dentro de `Minecraft::tick()`, 20 ticks depois que mundo/jogador existem —
  tempo suficiente para a posição final já ter chegado.
- **Bot andando rápido demais (~2x).** `BotPlayer::tick()` chamava
  `travel(xxa, yya)`/`jumpFromGround()` manualmente E chamava
  `Player::tick()` em seguida — mas `Player::tick()` já dispara
  `LivingEntity::aiStep()`, que por sua vez já chama `travel()` (com o devido
  amortecimento) sozinho para qualquer `Player` não-local rodando no lado
  servidor (`isEffectiveAi() == true`). O resultado era o movimento sendo
  aplicado duas vezes por tick. Corrigido removendo as chamadas manuais —
  `BotPlayer::tick()` agora só seta `xxa`/`yya`/`jumping` via o controller e
  deixa `Player::tick()` cuidar do resto, igual a qualquer outro mob.

## Do lado Python: geradores, não listas

O lado Python (`ai/environment.py`) processa um stream que, na prática, nunca
acaba enquanto o jogo estiver rodando (um `State` por tick de estado
enviado). Por isso todo o pipeline de leitura é feito com **geradores**, não
com listas acumuladas em memória:

- `iter_states(buf)` em `environment.py` é um gerador que produz um `State`
  por vez a partir do socket (`for raw_line in buf: ... yield ...`), nunca
  materializando a conexão inteira em memória.
- O laço principal (`for state in iter_states(buf): ...`) processa e descarta
  cada `State`/`Action` antes de pedir o próximo — uso de RAM constante,
  independente de quanto tempo o bot roda.

Ao estender `ai/` (política real na Etapa 3, memória espacial na Etapa 5,
ver `PLANO.md`), mantenha esse padrão: qualquer coisa que itere sobre
estados, ticks, chunks visitados ou datasets deve usar gerador (`yield`) ou
iterador preguiçoso, nunca uma lista construída de uma vez só — o histórico
pode crescer indefinidamente numa sessão de jogo longa.

## Limitações conhecidas do MVP

- Sem abrir containers/portas nem inventário completo (crafting,
  drag-and-drop) — ver Etapa futura no `PLANO.md`.
- O estado enviado é reduzido (posição, rotação, chão, vida, fome, id do
  bloco à frente) — biomas, blocos vizinhos e entidades próximas ficam para
  depois.
- `break_block`/`place_block` usam primitivas diretas de `Level`/`Tile` (não
  passam por `GameMode`/`ServerPlayerGameMode`), então não fazem checagem de
  durabilidade de ferramenta, XP ou som/partícula completos — só a mutação
  real do bloco no mundo.
- Comportamento de fome/física do bot durante `Player::tick()` não foi
  validado a fundo em runtime (o motor tem caminhos de tick bem diferentes
  para jogador local vs. jogador de rede) — se notar algo estranho (fome não
  baixando, física esquisita), é o primeiro lugar a investigar.
