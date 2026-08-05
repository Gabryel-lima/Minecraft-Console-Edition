# CuriousMob

Agente jogável controlado por um processo Python externo via uma ponte TCP,
com **paridade de mecânicas com o `Player` real**: os dois botões do mouse,
mira por raycast, inventário, containers, agachar/correr, uso de itens. Ver
`PLANO.md` para a visão completa e `protocol/messages.md` para o protocolo.

## Estrutura

```
mods/CuriousMob/
  PLANO.md            # plano completo (roadmap, etapas)
  ai/
    environment.py    # laço principal: conecta na ponte e joga
    policy.py         # CuriousPolicy (curiosidade+sobrevivência) e TrainedPolicy
    curiosity.py      # curiosidade por contagem (Etapa 2) e RND (Etapa 4)
    memory.py         # memória espacial por chunk (Etapa 5)
    env.py            # ambiente Gymnasium (Etapa 3)
    train.py          # treino PPO (Stable-Baselines3)
  protocol/           # spec do protocolo + (de)serialização Python
  tests/              # suíte pytest (inclui round-trip com o C++ real)
  docs/ models/ datasets/

4jcraft/Minecraft.Client/Mods/CuriousMob/   # código C++ do bot
  BotPlayer.{h,cpp}            # a entidade (extends Player)
  CuriousMobBridge.{h,cpp}     # servidor TCP local
  CuriousMobController.{h,cpp} # traduz JSON <-> chamadas do BotPlayer
  CuriousMobJson.{h,cpp}       # (de)serialização JSON, sem dependências
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
CURIOUSMOB_SPAWN=1 make run
```

Você deve ver um segundo "jogador" (visual padrão de player) aparecer a
poucos blocos de distância de onde você spawnou.

O bot sempre nasce em **modo sobrevivência** (`abilities` setado
explicitamente em `BotPlayer::BotPlayer`), mesmo que o mundo tenha sido
criado/aberto em modo criativo — o modo de jogo do mundo não é herdado pelo
bot porque ele nunca passa por `GameMode::initPlayer`. Isso é intencional:
sem sobrevivência não há fome, dano nem durabilidade, e metade das mecânicas
deixaria de existir para o agente.

### Teleportar até o bot (F4)

Com o bot spawnado, aperte **F4** a qualquer momento (fora de menus) para
teleportar o jogador local até a posição atual do bot. Útil para achá-lo
depois que ele andou/quebrou blocos e saiu do seu campo de visão. Não usamos
a tecla `F` simples porque ela já é a tecla de interagir/usar do jogo (ver
`4J.Input/4J_Input.cpp`).

## Rodar o agente Python

O agente **não precisa de nenhuma dependência externa** para jogar: o laço
principal, a política, a curiosidade por contagem e a memória usam só a
biblioteca padrão. Gymnasium/NumPy só entram no ambiente de RL, e
Torch/SB3 só no treino.

```sh
# Com o jogo já rodando (CURIOUSMOB_SPAWN=1), em outro terminal:
python3 mods/CuriousMob/ai/environment.py
```

Opções:

| comando | o que faz |
|---|---|
| `environment.py` | política `CuriousPolicy` (padrão) |
| `environment.py --random` | política aleatória da Etapa 1, para diagnosticar a ponte |
| `environment.py --rnd` | soma curiosidade RND (requer torch) |
| `environment.py --model models/ppo_curiousmob.zip` | política PPO treinada |
| `environment.py --no-memory-save` | não persiste a memória |

A memória espacial é gravada em `mods/CuriousMob/models/memory.json` a cada
2000 estados e no encerramento (inclusive Ctrl-C). Ela **é recarregada na
próxima execução**: sem isso, todo restart faria o mundo inteiro parecer
novo e a curiosidade por contagem não significaria nada.

## Treinar (PPO)

```sh
pip install -r mods/CuriousMob/ai/requirements-train.txt
# com o jogo rodando:
python3 mods/CuriousMob/ai/train.py --steps 100000 --rnd
```

O treino é **online contra o jogo ao vivo**: cada passo de PPO é um tick real
do Minecraft, então 100k passos levam horas de relógio. Não há paralelismo de
ambientes — há um bot só no mundo. Para acelerar, o caminho é diminuir
`STATE_SEND_INTERVAL` no C++ (mais decisões por segundo), não somar processos.

`reset()` não rebobina o mundo (o Minecraft não tem isso): ele só reconecta e
devolve a primeira observação. Os episódios terminam por tempo
(`--episode-steps`) ou pela morte do bot, então `truncated` é o caminho
normal de fim de episódio, não `terminated`.

## Testes

```sh
pip install pytest
python3 -m pytest mods/CuriousMob/tests -q
```

159 testes, ~2s. Cobrem:

- **protocolo** (`test_protocol.py`): parse do estado completo, tolerância a
  estado antigo/campo desconhecido, `chunk()` com coordenada negativa,
  omissão de campos `None`.
- **round-trip real com o C++** (`test_cpp_json.py`): compila
  `CuriousMobJson.cpp` com um harness e verifica os dois sentidos — o JSON
  que o C++ escreve é parseável pelo `json` do Python e fiel, e o que o
  `Action.to_json()` do Python escreve é lido pelo C++ com os mesmos
  valores. É o único teste que pega erros do serializador escrito à mão.
  Pulado se não houver `g++`/`clang++`.
- **paridade com o Player** (`test_player_parity.py`): ver a seção abaixo.
- **memória e curiosidade** (`test_memory_and_curiosity.py`): decaimento da
  novidade, bônus de bioma/bloco novo, atribuição de dano a chunk,
  persistência, e o limite de RAM do `recent_positions`.
- **política** (`test_policy.py`): ordem de prioridade das regras, limite de
  velocidade de virada, e que nenhuma ação produzida é inserializável.
- **ponta a ponta do lado Python** (`test_bridge_e2e.py`): sobe uma ponte
  falsa que fala o protocolo real e roda o `environment.py` de verdade
  contra ela — inclusive linha malformada, conexão recusada e mundo hostil.

## Paridade de mecânicas com o Player

O requisito do `PLANO.md` é que o agente seja **um jogador headless**, não um
mob com um punhado de ações. Concretamente, `BotPlayer` estende `Player`
(não `Mob`), e os dois cliques do mouse são roteados pelo alvo mirado
exatamente como o jogo roteia os do humano:

| | alvo é entidade | alvo é bloco | nada na mira |
|---|---|---|---|
| `attack` | `Player::attack` | quebra o bloco (com desgaste de ferramenta e drops) | — |
| `use` | `Player::interact` | `Tile::use`, e se o bloco não consumir, `ItemInstance::useOn` | `ItemInstance::use` |

**Isso é o ponto central do design.** Não existe uma ação por mecânica:
abrir porta, apertar botão, abrir baú/fornalha/bancada, acender fogo,
arar terra, encher balde, plantar muda, montar cavalo, tosquiar ovelha,
domesticar lobo, comer, beber poção e puxar o arco são todos `use`. O jogo
já implementa cada um dentro de `Tile::use`/`Item::useOn`/`Item::use`, e o
bot passa pelo mesmo caminho — então **qualquer mecânica que o motor ganhe
no futuro vale para o agente de graça**, sem código novo aqui.

Pela mesma razão, o bot **não burla o sistema de confiança**: `isAllowedToMine`,
`isAllowedToHurtEntity`, `isAllowedToInteract`, `isAllowedToUse` e
`mayDestroyBlockAt` são checados antes de cada ação destrutiva, como para um
jogador humano num mundo com o sistema ligado. (O construtor concede os
privilégios ao bot com `enableAllPlayerPrivileges(true)` — sem passar pela
rede ele nasceria sem nenhum e não conseguiria nem quebrar um bloco.)

`test_player_parity.py` automatiza a verificação disso em três camadas, para
não ser uma tautologia sobre o dataclass Python:

1. toda mecânica listada tem campo na `Action` (e toda observação, na `State`);
2. esse campo é **realmente lido/escrito** pelo `CuriousMobController.cpp`
   — verificado por leitura do fonte;
3. o `BotPlayer.cpp` roteia cada mecânica pela **mesma primitiva do motor**
   que o jogador real usa (`attack`, `interact`, `Tile::use`, `useOn`,
   `mineBlock`, `playerDestroy`, `playerWillDestroy`, `dropAll`,
   `closeContainer`).

Se alguém trocar `interact()` por uma reimplementação caseira, ou adicionar
um campo ao protocolo sem ligar no C++, o teste quebra.

### Cobertura atual

Ações: andar (categórico e analógico), strafe, pular, agachar, correr, virar
(yaw), olhar (pitch), atacar/minerar, usar bloco/entidade/item, quebrar e
colocar bloco, selecionar slot da hotbar, mover item entre slots, soltar item
/ pilha / inventário, soltar e cancelar item em uso, fechar container.

Observações: posição, rotação, velocidade, no chão / na água / na lava,
agachado, correndo, dormindo, usando item, vivo, vida, fome, saturação, ar,
experiência, hora do dia, luz, bioma, alvo sob a mira (bloco ou entidade),
blocos vizinhos, entidade mais próxima, inventário (itens + armadura + item
na mão), container aberto, e o resultado da última ação.

### O que os testes NÃO cobrem

Que a mecânica funcione **em runtime dentro do mundo**. Isso exige o jogo
rodando com um mundo carregado e é irredutivelmente manual — ver o roteiro
abaixo. Os testes cobrem que o caminho existe e está ligado ponta a ponta.

## Roteiro de verificação manual

Sem o Python conectado:
- [ ] O bot aparece no mundo e anda sozinho (fallback aleatório em
      `CuriousMobController::applyRandomWander`).

Com o Python conectado (`environment.py` rodando):
- [ ] O terminal do Python imprime uma linha a cada 100 ticks com posição,
      bioma, vida, fome, novidade e número de chunks conhecidos.
- [ ] Movimento: o bot anda, vira, pula, agacha e corre.
- [ ] Mira: `look_pitch` faz o bot olhar para cima/baixo (visível pela
      inclinação da cabeça).
- [ ] Minerar: `attack` com um bloco na mira remove o bloco **e dropa o
      item** (a diferença para a Etapa 1, que não passava por `playerDestroy`).
- [ ] Ferramenta: minerar com uma picareta na mão gasta durabilidade.
- [ ] Colocar bloco: com um bloco na hotbar selecionada, `use` mirando o
      chão coloca o bloco.
- [ ] Porta/botão/alavanca: `use` mirando um deles aciona o mecanismo.
- [ ] Baú: `use` num baú abre; o estado seguinte traz `container` não-nulo
      com o conteúdo; a política manda `close_container` e o baú fecha.
- [ ] Atacar: `attack` com uma entidade na mira causa dano nela.
- [ ] Interagir: `use` com um porco/cavalo na mira monta nele.
- [ ] Comer: com comida na mão e fome baixa, o bot come e a fome sobe.
- [ ] Soltar item: `drop` gera um `ItemEntity` no mundo.
- [ ] Sobrevivência: a fome do bot cai com o tempo; ele toma dano de queda.

## Do lado Python: geradores, não listas

O lado Python processa um stream que, na prática, nunca acaba enquanto o jogo
estiver rodando (um `State` por tick de estado enviado). Por isso todo o
pipeline de leitura é feito com **geradores**, não com listas acumuladas:

- `iter_states(buf)` produz um `State` por vez a partir do socket, nunca
  materializando a conexão inteira;
- as consultas da `Memory` (`visited_chunks`, `dangerous_chunks`,
  `frontier_chunks`) devolvem geradores;
- `Memory.recent_positions` é um `deque` com `maxlen`, não uma lista;
- as contagens da memória são por **chunk**, não por posição — o espaço de
  chaves cresce com o mundo explorado, não com o tempo de sessão.

Ao estender `ai/`, mantenha esse padrão: qualquer coisa que itere sobre
estados, ticks, chunks visitados ou datasets deve usar gerador (`yield`) ou
iterador preguiçoso. `test_memory_and_curiosity.py::test_recent_positions_is_bounded`
guarda essa invariante.

## Problemas encontrados e corrigidos

- **Bot invisível/longe do jogador.** O spawn rodava dentro de
  `Minecraft::setLevel()`, ponto em que `player->x/y/z` ainda é a posição
  placeholder pré-rede, não a posição final que o servidor atribui via
  `LoginPacket`. O bot spawnava a centenas de blocos. Corrigido adiando o
  spawn para dentro de `Minecraft::tick()`, 20 ticks depois que mundo e
  jogador existem.
- **Bot andando ~2x rápido demais.** `BotPlayer::tick()` chamava
  `travel()`/`jumpFromGround()` manualmente E chamava `Player::tick()` em
  seguida — mas `Player::tick()` já dispara `LivingEntity::aiStep()`, que já
  chama `travel()` sozinho para qualquer `Player` não-local rodando no lado
  servidor (`isEffectiveAi() == true`). Corrigido removendo as chamadas
  manuais.
- **Campo de ação confundido com valor de string.** A busca de campo do JSON
  em C++ casava a ocorrência de `"forward"` como **valor** de
  `{"move": "forward"}` e lia o número do campo seguinte — o bot andava com
  um `forward` que ninguém mandou. Corrigido exigindo que a ocorrência seja
  seguida de `:` para contar como chave. Regressão coberta por
  `test_cpp_json.py::test_cpp_does_not_confuse_key_with_string_value`.

## Limitações conhecidas

- **Crafting e drag-and-drop de slots** dentro de um container aberto não são
  expostos. O container é observável e abrir/fechar funciona, mas manipular
  os slots exigiria replicar `AbstractContainerMenu::clicked` fora do menu.
- **Ids numéricos, não nomes.** Blocos e itens trafegam como id; a política
  tem uma tabela mínima em `policy.py` (`TILE_CHEST`, `TILE_LAVA`, ...) mas
  não há mapa completo id -> nome.
- **A ponte é Linux-only** (socket POSIX escrito do zero; o código de rede
  vanilla é acoplado ao protocolo QNet do console e não é reaproveitável).
- **Um bot por mundo.** A ponte escuta numa porta fixa e o
  `CuriousMobController` a instancia no construtor do bot; múltiplos bots
  exigiriam uma porta por bot ou multiplexação.
- **`RNDCuriosity` e `TrainedPolicy` degradam graciosamente**: sem
  torch/SB3 instalados, `build_curiosity` cai para a versão por contagem em
  vez de falhar. Se você passou `--rnd` e a novidade parece só contagem,
  confira se o torch está mesmo instalado.
