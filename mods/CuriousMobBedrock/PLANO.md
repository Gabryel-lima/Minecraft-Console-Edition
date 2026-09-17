# PLANO.md — CuriousMob Bedrock (agente de RL empacotável no Minecraft Bedrock atual, Windows)

> Porte da frente `mods/CuriousMob/` (agente curioso com aprendizado por reforço
> dentro desta reconstrução em C++ da Console Edition) para o **Minecraft
> Bedrock atual rodando no Windows**, empacotado como **add-on oficial**
> (`.mcaddon`) instalável no jogo.
>
> Este documento é **plano**, não relatório: nada abaixo está implementado.
> Cada etapa só vira "✅" com commit linkado, como em `mods/CuriousMob/PLANO.md`.

## Por que este documento existe (e o que ele NÃO é)

O pedido foi "pegar o mod do agente de RL e aplicar no Bedrock atual do
Windows, no mesmo formato, para empacotar dentro do jogo". Antes do roadmap,
três correções de premissa que mudam o desenho inteiro — se elas não forem
aceitas, o resto do plano não se sustenta:

1. **Não existe "portar o `BotPlayer.cpp`".** O Bedrock é closed-source: não
   há como derivar de `Player`, não há `Tile::use`, não há recompilação. A
   única superfície de extensão legítima é a **Scripting API de add-ons**
   (`@minecraft/server` e módulos irmãos), em JavaScript/TypeScript, rodando
   num interpretador embutido com API restrita. Os ~1.600 linhas de C++ em
   `4jcraft/Minecraft.Client/Mods/CuriousMob/` **são descartadas**, não
   traduzidas.
2. **O que se porta de verdade é o protocolo e o lado Python.** `ai/` +
   `protocol/` (~1.300 linhas: política, curiosidade por contagem, RND,
   memória por chunk, env Gymnasium, treino PPO, testes) são
   *engine-agnósticos* — eles só conhecem os dataclasses `State`/`Action`.
   Se o add-on falar o **mesmo schema JSON**, tudo isso roda sem uma linha
   alterada. **Esse é o ativo real da frente atual, e é ele que o plano
   protege.** O trabalho novo é: um controller em TypeScript + um transporte
   novo + o empacotamento.
3. **"Empacotar o mod dentro do jogo" tem um limite duro no cliente de
   varejo.** Um `.mcaddon` instala scripts, mas os scripts **não têm
   sockets**. O módulo de rede (`@minecraft/server-net`) só existe no
   **Bedrock Dedicated Server (BDS)**, não no cliente da Microsoft Store.
   Ou seja: o add-on é instalável no cliente (mundo, entidade, comportamento),
   mas a **ponte com o Python só fecha em BDS** — ou, com limitações severas,
   via o canal `/connect` WebSocket (Plano B, seção "Transporte").
   Prometer "instala no Minecraft da Store e treina PPO" seria mentira.

Consequência de projeto: o alvo primário é **BDS no Windows** (gratuito,
oficial, roda em Windows 10/11), com o jogador humano entrando pelo cliente
de varejo em `127.0.0.1` para assistir. O `.mcaddon` continua sendo o
artefato de distribuição, e o mesmo pacote instala no cliente — só que sem a
ponte.

## Objetivos (herdados de `mods/CuriousMob/PLANO.md`, sem mudança)

Um agente que explore espontaneamente, desenvolva comportamento próprio,
memorize lugares, demonstre interesse por novidade e continue aprendendo —
com **paridade de ações com o jogador**, não um mob com um punhado de ações.

Objetivo específico desta frente: **manter a compatibilidade de protocolo**
com `mods/CuriousMob/protocol/messages.md`, para que `policy.py`,
`curiosity.py`, `memory.py`, `env.py` e `train.py` rodem contra os dois
motores sem fork.

## Regras invioláveis

Herdadas de `MODERNIZACAO.md` e estendidas para o contexto de add-on:

1. **Nenhum asset da Mojang/Microsoft no repo.** Add-on só contém JSON/TS
   escritos aqui. Nada de texturas, sons, modelos ou `.json` de vanilla
   copiados.
2. **Nada de engenharia reversa do binário do Bedrock, memória do processo
   ou protocolo de rede.** Só a API pública documentada de creators. Isso não
   é escrúpulo decorativo: injeção em processo / leitura de memória é
   exatamente o que o EULA e o anti-cheat tratam como cheat, e inviabilizaria
   distribuir o resultado.
3. **O agente joga no mundo local do usuário.** Nunca usar isto em servidor
   de terceiros/realm — um bot controlado externamente em servidor alheio é
   cheat, independentemente de a API ser oficial.
4. **Especificação antes de código**, como no fluxo de `MODERNIZACAO.md`:
   toda divergência de comportamento entre 4jcraft e Bedrock vira entrada
   escrita neste documento antes de virar TypeScript.

## Arquitetura alvo

```
Bedrock Dedicated Server (Windows)         Processo Python (reaproveitado)
┌──────────────────────────────────┐       ┌─────────────────────────────┐
│ behavior_pack CuriousMobBedrock  │       │ ai/transport_http.py  (novo)│
│   main.ts                        │       │ ai/environment.py           │
│     system.runInterval(N ticks)  │──────▶│ ai/policy.py                │
│     buildState()  ─ JSON State ─ │ HTTP  │ ai/curiosity.py             │
│     applyAction() ◀─ JSON Action │◀──────│ ai/memory.py                │
│   bot.ts    (SimulatedPlayer)    │       │ ai/env.py → train.py (PPO)  │
│   ids.ts    (string ⇄ id inteiro)│       │ protocol/messages.py        │
└──────────────────────────────────┘       └─────────────────────────────┘
        ▲                                             (sem alteração)
        │ entra pelo cliente de varejo p/ assistir
   Minecraft for Windows
```

Equivalência 1:1 com a frente atual:

| 4jcraft (C++)                | CuriousMobBedrock (TS)            |
|---|---|
| `BotPlayer` (extends `Player`) | `SimulatedPlayer` (`@minecraft/server-gametest`) |
| `CuriousMobController.cpp`     | `controller.ts` (build state / apply action) |
| `CuriousMobBridge.cpp` (TCP)   | `bridge.ts` (`@minecraft/server-net`, HTTP) |
| `CuriousMobJson.cpp`           | `JSON` nativo — some, e com ele a classe inteira de bugs de serializador |
| `CURIOUSMOB_SPAWN=1`           | `/scriptevent curiousmob:spawn` |
| `STATE_SEND_INTERVAL`          | período do `system.runInterval` |

## O agente: `SimulatedPlayer`, e por que ele é a escolha certa

`@minecraft/server-gametest` expõe `SimulatedPlayer`, que **estende `Player`**
no próprio modelo de objetos do Bedrock. Isso reproduz, sem esforço, a
decisão central do desenho atual: as ações do agente passam pelas **mesmas
primitivas do motor** que as do humano, então mecânica nova do jogo vale
para o agente de graça.

Mapa direto das primitivas (a tabela do `README.md` atual, traduzida):

| ação do protocolo | 4jcraft | Bedrock |
|---|---|---|
| `attack` (entidade) | `Player::attack` | `attackEntity()` / `attack()` |
| `attack` (bloco) | `mineBlock`/`playerDestroy` | `breakBlock()` / `stopBreakingBlock()` |
| `use` (bloco) | `Tile::use` → `useOn` | `interactWithBlock()` / `useItemOnBlock()` |
| `use` (entidade) | `Player::interact` | `interactWithEntity()` |
| `use` (vazio) | `ItemInstance::use` | `useItemInSlot()` |
| `move`/`forward`/`strafe` | `xxa`/`yya` | `moveRelative()` / `move()` |
| `turn`/`look_pitch` | `yRot`/`xRot` | `setRotation()` / `lookAtBlock()` |
| `jump`/`sneak`/`sprint` | flags do input | `jump()`, `isSneaking`, `isSprinting` |
| `select_slot` | `Inventory::selectSlot` | `selectedSlotIndex` |
| `drop*` | `dropAll`/`drop` | `dropSelectedItem()` / container API |
| `close_container` | `closeContainer` | fecha sozinho / `stopUsingItem()` |

**Riscos honestos desta escolha, a verificar na Etapa 0** (não são detalhes —
qualquer um deles derruba o plano A):

- `@minecraft/server-gametest` é **beta API**: exige o toggle experimental
  "Beta APIs" no mundo, e a assinatura muda entre versões. Um mundo com beta
  ligado não recebe conquistas e pode não abrir em versão futura.
- O spawn de `SimulatedPlayer` historicamente é **escopado a um GameTest**
  (`test.spawnSimulatedPlayer`), não uma chamada livre de dimensão. Se ainda
  for assim na versão alvo, o add-on precisa registrar um gametest "infinito"
  (`register(...).maxTicks(...)` grande) como hospedeiro do bot — feio, mas
  funcional e já usado pela comunidade. **Verificar antes de qualquer código.**
- `SimulatedPlayer` não sofre fome em algumas versões / não persiste no save.
  Se fome não existir, `state.food` vira constante e metade da pressão de
  sobrevivência some — nesse caso, documentar e ajustar `env._reward`, nunca
  fingir o campo.

**Plano C (se `SimulatedPlayer` for inviável na versão alvo):** entidade
customizada (`minecraft:entity` com behavior pack) dirigida por script. Perde
inventário/crafting/parity-com-Player — ou seja, perde exatamente o requisito
central da frente. Só como último recurso, e com o custo escrito aqui.

## Transporte

### Plano A — BDS + `@minecraft/server-net` (alvo primário)

- O jogo vira **cliente HTTP**; o Python vira **servidor HTTP** (inversão de
  papéis em relação ao TCP de hoje, onde o jogo é o servidor).
- `http.request(new HttpRequest("http://127.0.0.1:5555/step"))`, POST com o
  `State` no corpo, resposta com o `Action`. Um round-trip por envio de
  estado (a cada N ticks), não por tick.
- Exige, no BDS: `config/<uuid>/permissions.json` liberando
  `@minecraft/server-net`, e o módulo declarado no `manifest.json`.
- `http.request` é **assíncrono** (Promise). O laço tem que tolerar resposta
  atrasada exatamente como o `recv` não-bloqueante do C++ hoje: **a última
  ação recebida é a que vale**, respostas fora de ordem são descartadas por
  `tick`. Isso já é o contrato do protocolo atual ("a última ação recebida é
  a que vale no próximo tick") — não muda nada do lado Python.

### Plano B — cliente de varejo via `/connect` WebSocket

Para quem não quer rodar BDS. O cliente aceita `/connect ws://localhost:PORT`
e fala um protocolo de comandos/eventos. Duplex possível, mas capenga:

- Python → jogo: envia comandos (`/scriptevent curiousmob:act {json}`), que o
  add-on recebe em `system.afterEvents.scriptEventReceive`.
- jogo → Python: `world.sendMessage(json)` capturado pela subscrição
  `PlayerMessage` do WebSocket.
- Custos: limite de tamanho de mensagem, latência maior, eventos de telemetria
  progressivamente removidos pela Mojang em versões recentes, e o canal já foi
  quebrado/reabilitado mais de uma vez. **Tratar como degradação demonstrável,
  não como caminho de treino.**

### Plano D (rejeitado, registrado para não voltar à mesa)

Visão + injeção de input no cliente (captura de tela + `pyautogui`), estilo
MineRL/MineDojo. Rejeitado: quebra a paridade de ações (o agente passa a
observar pixels), é frágil a resolução/HUD/idioma, e é indistinguível de cheat
para qualquer anti-cheat. Só faria sentido se o objetivo fosse pesquisa de
percepção visual — não é o desta frente.

## Divergências de protocolo a resolver (spec antes de código)

O schema de `protocol/messages.md` é o contrato. As divergências abaixo são
*todas* as que conheço hoje; cada uma precisa de decisão escrita:

1. **Ids: numéricos aqui, strings no Bedrock.** Lá é `"minecraft:stone"`, não
   `1`. Opção escolhida: `ids.ts` mantém uma **tabela de interning
   persistida** (`models/bedrock_ids.json`) mapeando string → inteiro estável,
   e o protocolo ganha campos opcionais `*_name` paralelos aos ids. Assim
   `policy.py` (`TILE_CHEST`, `TILE_LAVA`, ...) continua válido *se* semearmos
   a tabela com os ids legados desses blocos, e de quebra **resolve a
   limitação "ids numéricos, não nomes"** listada no plano atual. Alternativa
   descartada: hash de string (colide, e id instável entre execuções destrói
   a memória por chunk).
2. **Campos possivelmente indisponíveis**: `saturation`, `air`, `light`,
   `biome`. Componentes/APIs equivalentes existem parcialmente e variam por
   versão. Regra: **campo indisponível é omitido**, nunca inventado —
   `State.from_json` já cai no default (tolerância que o protocolo tem de
   propósito). Cada omissão vira linha numa tabela de cobertura no README.
3. **`target`**: `entity.getBlockFromViewDirection()` /
   `getEntitiesFromViewDirection()` cobrem o raycast, com a mesma regra de
   "entidade tem prioridade quando mais perto".
4. **`container`**: legível via `block.getComponent("minecraft:inventory")`.
   Crafting e drag-and-drop continuam **fora de escopo**, como hoje.
5. **`last_result`**: no C++ vem do retorno das primitivas. No Bedrock várias
   chamadas não retornam sucesso/falha. Onde não houver retorno, derivar por
   diferença de estado (bloco sumiu? vida da entidade caiu?) e **documentar
   que é inferido**, não observado.

## Etapas

### Etapa 0 — Verificação de viabilidade (bloqueante, antes de qualquer código)
Um mundo de teste e um add-on de 20 linhas que respondem às perguntas que
derrubam o plano:
- [ ] Versão alvo do Bedrock e do `@minecraft/server` (matriz de versões da
      docs oficial) — fixar e anotar aqui.
- [ ] `SimulatedPlayer` spawna? Só dentro de gametest? Sobrevive a
      `maxTicks` longo? Tem fome?
- [ ] `@minecraft/server-net` funciona no BDS Windows com
      `permissions.json`? Latência do round-trip local.
- [ ] Watchdog: quanto tempo de script por tick antes de matar o pacote?
- **Saída:** esta seção preenchida com respostas + decisão A/B/C.

### Etapa 1 — Esqueleto do add-on e ponte
- Estrutura `behavior_packs/CuriousMobBedrock/` com `manifest.json`
  (módulos `script` + dependências `@minecraft/server`, `-gametest`, `-net`).
- Build TypeScript → bundle único com esbuild (o engine não resolve `node_modules`).
- `bridge.ts`: `system.runInterval` a cada N ticks, POST do estado, aplicação
  da última ação recebida.
- Fallback de "andar aleatório" quando o Python não responde — paridade com
  `applyRandomWander`.
- **Critério de pronto:** bot anda sozinho; `environment.py --random` faz ele
  andar pela ponte.

### Etapa 2 — Paridade de ações
Implementar todos os campos de `Action`, roteando `attack`/`use` pelo alvo
mirado, **sem inventar uma ação por mecânica**.
- **Critério de pronto:** `test_player_parity.py` adaptado passa (ver Testes).

### Etapa 3 — Observações completas
`buildState()` com tudo que a versão permitir + tabela de cobertura honesta
do que ficou de fora.
- **Critério de pronto:** `test_protocol.py` (o mesmo de hoje) passa contra
  estados capturados do jogo real.

### Etapa 4 — Reuso do lado Python
Refatorar `ai/` para separar **transporte** de **laço**:
`transport_tcp.py` (4jcraft, atual) e `transport_http.py` (Bedrock, novo),
com `environment.py`/`env.py` recebendo o transporte por injeção.
`policy.py`, `curiosity.py`, `memory.py`, `train.py` **não mudam**.
- **Critério de pronto:** a suíte atual passa nos dois transportes.

### Etapa 5 — Empacotamento `.mcaddon`
- `make bedrock-pack` gerando o zip renomeado, com versionamento no manifest.
- Instalação dev: copiar para
  `%LOCALAPPDATA%\Packages\Microsoft.MinecraftUWP_*\LocalState\games\com.mojang\development_behavior_packs\`
  (cliente) e `behavior_packs/` + `world_behavior_packs.json` (BDS).
- Instalação de usuário: duplo clique no `.mcaddon`.
- **Critério de pronto:** instalação limpa numa máquina Windows sem o repo.

### Checkpoints: salvar e carregar (requisito confirmado)

"Ponto de salvamento e carregamento" tem **duas leituras**, e elas não são
alternativas — são duas metades que só valem juntas:

- **Estado do mundo** (o save do Bedrock: blocos, entidades, hora, inventários
  dos baús). É o que permite *rebobinar o jogo*.
- **Estado do aprendizado** (pesos do PPO, memória por chunk, RND, tabela de
  ids). É o que permite *rebobinar o agente*.

Salvar um sem o outro produz incoerência silenciosa: se o mundo volta para o
tick 10.000 mas a `Memory` continua sabendo dos chunks visitados até o tick
80.000, a curiosidade por contagem passa a descrever um futuro que foi
desfeito — o agente deixa de explorar uma região que, para o mundo, nunca foi
visitada. **Um checkpoint aqui é, por definição, atômico e composto.**

E isto é um ganho concreto sobre a frente C++: o `PLANO.md` atual registra
como limitação que "`reset()` não rebobina o mundo (o Minecraft não tem
isso)". No BDS **tem** — com um custo, descrito abaixo.

### Os dois níveis (e por que dois, não um)

| | **Soft checkpoint** | **Hard checkpoint** |
|---|---|---|
| o que restaura | bot (posição, rotação, vida, fome, inventário, slot), hora do dia, clima | o mundo inteiro, byte a byte, + todo o estado do agente |
| como | script do add-on, `teleport` + componentes + `Container.setItem` | arquivos do save do BDS |
| custo | ~1 tick | parar o BDS, trocar a pasta do mundo, reiniciar (~10–60 s) |
| o que **não** restaura | blocos quebrados, mobs mortos, itens dropados, baús mexidos | — |
| uso | `reset()` de episódio, a cada episódio | rebobinar experimento, sair de estado corrompido, comparar políticas no mesmo mundo |

Um nível só não resolve: restaurar o mundo a cada episódio (a cada ~2000
passos, hoje) custaria um restart de servidor por episódio e mataria o
throughput; e restaurar só o bot deixa o mundo acumular danos irreversíveis
ao longo de horas (cavernas escavadas, mobs extintos na região), o que é uma
mudança lenta de distribuição que ninguém percebe até a curva de recompensa
ficar estranha.

### Hard checkpoint: como o BDS permite isso

O BDS expõe o procedimento oficial de backup a quente, pelo console do
servidor (stdin/stdout do processo):

1. `save hold` — pausa a escrita no disco;
2. `save query` (repetir até responder) — devolve a lista de arquivos do save
   **com o comprimento em bytes de cada um**;
3. copiar os arquivos, **truncando cada um no comprimento informado** — este
   detalhe não é opcional: o LevelDB pode ter apêndices posteriores ao ponto
   consistente, e copiar o arquivo inteiro produz um save que abre e só
   depois corrompe;
4. `save resume` — o mais rápido possível (o hold segura escrita em memória).

Restaurar **não** é a quente: parar o BDS, substituir `worlds/<mundo>/`,
subir de novo. Daí o custo do nível "hard".

Isso exige uma peça de infra nova: **`ai/bds_supervisor.py`**, que sobe o BDS
como subprocesso, fala com o console dele por stdin/stdout, e expõe
`hold()/query()/resume()/stop()/start()`. Sem supervisor, não há checkpoint
de mundo — é o primeiro item da Etapa 5.5.

### Anatomia de um checkpoint

```
checkpoints/run-<id>/ck-<num_timesteps>/
  manifest.json        # a fonte de verdade — ver abaixo
  world/               # cópia truncada do save do BDS
  bot.json             # posição, inventário, vida, fome, slot do SimulatedPlayer
  ppo.zip              # policy + optimizer + num_timesteps (SB3)
  memory.json          # Memory por chunk
  rnd.pt               # target + predictor + optimizer + Welford (mean/var/count)
  ids.json             # tabela de interning string -> id inteiro
```

`manifest.json` carrega o que torna o load **recusável**: `run_id`, tick do
jogo, `num_timesteps` do PPO, versão do protocolo, versão do Bedrock/BDS,
versão do add-on, `sha256` de cada arquivo, timestamp, e as seeds de RNG.
Regra: **carregar um checkpoint incompatível falha alto**, nunca "tenta e vê".
Treinar horas em cima de estado incoerente é pior do que não carregar.

Escrita atômica: gravar em `ck-<n>.tmp/`, só depois escrever o manifest e
renomear o diretório (`os.replace`). Um checkpoint meio-escrito nunca pode
existir com manifest válido. No load, conferir os hashes antes de usar.

### `bot.json` existe porque o `SimulatedPlayer` provavelmente não persiste

A verificar na Etapa 0: se o `SimulatedPlayer` **não** entra no save do mundo
(hipótese mais provável — ele é uma construção de teste), então o save do BDS
restaura tudo *menos o agente*, e o bot precisa ser re-spawnado e
re-equipado por script a partir do `bot.json`. Se ele persistir, `bot.json`
vira redundante para o hard e continua necessário para o soft. Os dois casos
custam a mesma linha de código; o que não pode é descobrir isso depois de
restaurar um checkpoint e ver o bot nascer pelado no spawn do mundo.

### Defeito real encontrado na frente atual (vale para as duas)

`RNDCuriosity` (`ai/curiosity.py`) **não persiste nada**: nem a rede-alvo,
nem os pesos do preditor, nem o Welford (`_mean`/`_var`/`_count`). A
rede-alvo é sorteada de novo a cada execução. Consequência: retomar
`ppo_curiousmob.zip` com `--rnd` **retoma a política contra uma função de
recompensa intrínseca diferente** — o "oráculo" arbitrário que o preditor
imitava virou outro oráculo. A escala também recomeça (Welford zerado),
que é exatamente o problema que a normalização Welford existe para evitar.

Isso não é específico do Bedrock; é um bug da frente atual que só aparece
em treino longo com retomada. Corrigir junto (`RNDCuriosity.save/load`) e
anotar nos dois planos, como manda a regra de frentes cruzadas.

Da mesma lista: `train.py` chama `model.learn(total_timesteps=...)` sem
`reset_num_timesteps=False` na retomada — o contador e o cronograma de
learning rate reiniciam do zero a cada `--resume`.

### Como isso aparece no protocolo

O protocolo hoje só tem `State`/`Action`. Checkpoint precisa de um canal de
controle, e a forma barata é aproveitar a tolerância que o protocolo já tem:

- `Action` ganha um campo opcional `control`
  (`{"soft_reset": {...}} | {"snapshot_bot": true}`);
- `State` ganha `checkpoint_ack` opcional (o que foi aplicado, em que tick).

Campos desconhecidos são ignorados dos dois lados (`State.from_json` é
tolerante por desenho), então **o 4jcraft continua funcionando sem saber que
`control` existe**. O hard checkpoint não passa pelo protocolo: é orquestrado
pelo `bds_supervisor.py`, fora do jogo.

### Política de retenção (não é detalhe)

Um mundo Bedrock explorado por horas chega facilmente a centenas de MB. Um
hard checkpoint por episódio enche o disco em uma tarde. Padrão proposto:
hard a cada N passos de PPO (`CheckpointCallback` do SB3 como gatilho),
retendo os **3 mais recentes + 1 por hora + os marcados à mão**; soft não
gera arquivo. `checkpoints/` entra no `.gitignore` — save de mundo nunca vai
para o repo.

### Determinismo: o que um checkpoint NÃO dá

Restaurar estado **não** é replay determinístico. Random ticks, IA de mob,
spawns e o RNG interno do servidor não são restaurados pelo save. Duas
execuções a partir do mesmo checkpoint divergem em segundos. Logo:

- um checkpoint dá **condições iniciais comparáveis**, não trajetória
  reproduzível;
- comparar duas políticas a partir do mesmo checkpoint exige **N execuções e
  média**, não uma execução de cada — uma execução compara sorte, não política.

Quem pular essa distinção vai "medir" melhoras que são ruído.

### Etapa 5.5 — Checkpoints (entra entre o empacotamento e o treino)

- [ ] `ai/bds_supervisor.py`: BDS como subprocesso, console por stdin/stdout,
      `hold/query/resume/stop/start`, parse do `save query` com truncamento.
- [ ] `ai/checkpoint.py`: escrita atômica, manifest com hashes, `save()`/`load()`,
      retenção.
- [ ] `RNDCuriosity.save/load` + `reset_num_timesteps=False` na retomada
      (corrige a frente atual também).
- [ ] `control.soft_reset` no add-on: teleporte, inventário, vida/fome, hora.
- [ ] `CuriousMobEnv.reset(hard=...)`: soft por padrão, hard sob demanda,
      tolerante ao servidor sumir e voltar (o transporte HTTP precisa
      reconectar, não morrer).
- [ ] Testes: round-trip de checkpoint (salva → carrega → estado idêntico),
      recusa de manifest incompatível, recusa de hash quebrado, checkpoint
      interrompido no meio não é carregável.
- **Critério de pronto:** matar o treino no meio, recarregar o último
  checkpoint e o agente continuar do mesmo `num_timesteps`, no mesmo mundo,
  com a mesma memória e a mesma função de recompensa intrínseca.

## Etapa 6 — Treino e observação
Rodar PPO contra o BDS. **Vantagem real sobre a frente atual:** o limite "um
bot por mundo" cai — N instâncias de BDS em portas diferentes = `SubprocVecEnv`
de verdade, coisa que o 4jcraft não permite. O gargalo passa a ser CPU/RAM,
não arquitetura.
- **Critério de pronto:** curvas de recompensa de uma sessão longa + o roteiro
  manual do README preenchido.

## Testes (espelhando a suíte atual, que é o padrão de qualidade da frente)

| teste atual | equivalente Bedrock |
|---|---|
| `test_protocol.py` | **reaproveitado sem mudança** |
| `test_cpp_json.py` (round-trip com o C++ real) | `test_ts_json.py`: roda o bundle sob Node e confere os dois sentidos |
| `test_player_parity.py` | mesma ideia em 3 camadas: campo existe / é lido por `controller.ts` / usa a primitiva de `SimulatedPlayer` (não uma reimplementação) |
| `test_bridge_e2e.py` | ponte HTTP falsa no lugar do socket falso |
| `test_policy.py`, `test_memory_and_curiosity.py` | **reaproveitados sem mudança** |

Manter a disciplina de **geradores, não listas** do lado Python (o stream não
acaba enquanto o jogo roda) e, do lado TS, o equivalente: `system.runJob` com
generator para qualquer varredura pesada, para não bater no watchdog.

## Riscos, com o custo de cada um

| risco | probabilidade | impacto | mitigação |
|---|---|---|---|
| `SimulatedPlayer` inviável/instável na versão alvo | média | **fatal p/ paridade** | Etapa 0 antes de tudo; Plano C documentado |
| Beta API muda e quebra o add-on | alta | médio | fixar versão do Bedrock no README; testar antes de atualizar |
| `server-net` só em BDS frustra "instalar no jogo" | **certo** | médio | dito na primeira seção; Plano B como demo |
| Latência HTTP > 1 tick degrada controle | média | médio | decimar estado (N ticks), ação latch, medir na Etapa 0 |
| Watchdog mata o pacote em estado grande | média | baixo | `runJob`, estado enxuto (só slots ocupados, como hoje) |
| Hard checkpoint corrompe o save (cópia sem truncar) | média | **alto** | seguir `save query` à risca; teste de round-trip na Etapa 5.5 |
| Disco estourado por retenção de mundos | alta | médio | política de retenção + compressão + `.gitignore` |

## O que este plano deliberadamente NÃO promete

- Não promete substituir a frente 4jcraft. As duas convivem: a C++ continua
  sendo onde dá para mexer no motor; a Bedrock é onde o agente joga o jogo
  real, com as mecânicas modernas. `MODERNIZACAO.md` fica ainda mais
  justificado: é lá que se decide o que da mecânica moderna vale reimplementar
  no motor próprio.
- Não promete acelerar o treino além de 20 TPS. Bedrock não tem tick
  acelerado; o ganho é paralelismo de instâncias, não velocidade por instância.
- Não promete crafting/drag-and-drop de slots — continua fora de escopo,
  pelo mesmo motivo de hoje.
- Não promete **reprodutibilidade determinística** a partir de um checkpoint.
  Restaura estado, não trajetória — ver "Determinismo" na seção de checkpoints.

## Decisões já tomadas

- **Objetivo escolhido: o agente jogando o jogo real.** Logo, o alvo é o
  **Plano A (BDS + `@minecraft/server-net`)**. O `.mcaddon` continua sendo o
  formato de distribuição, mas "instala no cliente da Store e treina" está
  fora — o cliente não tem a ponte.
- **Checkpoints são requisito, não extra** (ver Etapa 5.5), e são atômicos:
  mundo + agente juntos ou nenhum dos dois.

## Próximo passo

Executar a **Etapa 0** e preencher suas respostas aqui. Nenhuma linha de
TypeScript antes disso: as quatro perguntas daquela etapa decidem se o Plano A
sobrevive ao contato com a versão alvo, e escrever código antes é apostar sem
evidência. À lista da Etapa 0 soma-se, por causa dos checkpoints:

- [ ] O `SimulatedPlayer` persiste no save do mundo? (decide se `bot.json` é
      necessário para o hard checkpoint ou só para o soft)
- [ ] `save hold`/`save query`/`save resume` funcionam pelo stdin do BDS no
      Windows, e quanto tempo leva o ciclo num mundo de ~100 MB?
- [ ] Quanto tempo leva um restart de BDS (o piso do custo de um hard reset)?
