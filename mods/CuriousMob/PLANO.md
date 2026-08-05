# PLANO.md — CuriousMob (adaptado para este projeto)

> Versão adaptada do plano original (pensado para Minecraft Java + Fabric) para
> a stack real deste repositório: **C++ / Meson / Ninja**, porta em C++ do
> Minecraft Console Edition (Xbox 360/PS3 → PC), sem Fabric, sem Java, sem mod
> loader. Ver `mods/CuriousMob/README.md` para instruções de uso e
> `mods/CuriousMob/protocol/messages.md` para o protocolo.

## Objetivos (sem mudança em relação ao plano original)

Criar um agente que:
- explore o mundo espontaneamente;
- desenvolva comportamento próprio;
- memorize locais visitados;
- demonstre interesse por novidades;
- aprenda continuamente.

Com o **requisito adicional** definido durante o planejamento deste projeto:
o agente deve ter, na medida do possível, **paridade de ações com o Player**
(quebrar/colocar bloco, atacar, usar item, abrir containers, inventário) —
não é um "mob" com um punhado fixo de ações, e sim um jogador headless
controlado externamente.

## Tecnologias (ajustado)

### Jogo
- C++ (Minecraft Console Edition, porta PC)
- Build: Meson + Ninja
- Sem mod loader — extensão via novas classes C++ isoladas em diretórios
  próprios (`4jcraft/Minecraft.Client/Mods/CuriousMob/`), compiladas
  automaticamente pelo `find` recursivo do Meson.

### IA
- Python 3.12+
- Gymnasium, NumPy (MVP)
- PyTorch + Stable-Baselines3 (entram só na etapa de treino real, Etapa 3+)

### Comunicação
- Socket TCP simples (`CuriousMobBridge`), JSON por linha. Ver
  `protocol/messages.md`. (O código de rede vanilla deste projeto é acoplado
  ao protocolo QNet do console e não serve para isso.)

## Arquitetura

```
Jogo (4jcraft, C++)
  BotPlayer (subclasse de Player/ServerPlayer)
  ↓ tick()
CuriousMobBridge (servidor TCP local)
  ↓ estado (JSON)         ↑ ação (JSON)
Processo Python (mods/CuriousMob/ai/)
  environment.py → policy.py (futuro) → curiosity.py / memory.py (futuro)
```

## Estado do que foi implementado

> **Situação atual: Etapas 1 a 5 implementadas.** O que falta é rodar o
> treino de verdade (horas de jogo) e observar a Etapa 6, que por definição
> não se implementa — se observa. Ver "O que falta" no fim desta seção.

### Etapa 1 (MVP da ponte)
- `BotPlayer`: agente no mundo, com aparência/skin padrão de player, herdando
  de `Player`/`ServerPlayer` (não de `Mob`), o que já dá acesso "de graça" a
  `Inventory`, `attack()`, `interact()`, abertura de containers etc.
- Movimento (frente/trás/esquerda/direita/virar/pular) aplicado via os mesmos
  campos que o motor usa para input real (`xxa`/`yya`/`jumping`/`yRot`/`xRot`),
  com fallback de "andar aleatório" quando a ponte Python não está conectada.
- Quebrar bloco, colocar bloco (usando o item selecionado no inventário) e
  atacar a entidade mais próxima — via `Player::attack()` e chamadas diretas
  a `Level`/`Tile` (`destroyTile`/`setTileAndData`), em vez de
  `GameMode`/`ServerPlayerGameMode` (ambos acoplados a um `Minecraft*`/
  `ServerPlayer` vivos, com timing de inicialização incerto para um bot —
  ver nota de implementação abaixo).
- Ponte TCP funcional ponta a ponta: estado enviado a cada N ticks, ação lida
  de volta de forma não bloqueante.
- Spawn de debug controlado por variável de ambiente (`CURIOUSMOB_SPAWN`),
  não por um comando de jogo — o sistema de comandos deste projeto é um
  registro fechado (`EGameCommand`, ver `Commands/CommandsEnum.h`) disparado
  só por pacote de rede, sem parser de texto livre; estender esse enum só
  para um spawn de dev seria desproporcional. O hook fica em
  `Minecraft.cpp`, logo após o jogador local ser adicionado ao mundo.
- Esqueleto Python (`environment.py` com política aleatória, `policy.py`,
  `curiosity.py`, `memory.py` como stubs).

### Paridade com o Player (o requisito adicional, agora cumprido)

O ponto de projeto que resolveu isto: **não existe uma ação por mecânica.**
Os dois cliques do mouse (`attack`/`use`) são roteados pelo alvo mirado
exatamente como o jogo roteia os do humano —

| | alvo é entidade | alvo é bloco | nada na mira |
|---|---|---|---|
| `attack` | `Player::attack` | quebra o bloco (desgaste de ferramenta + drops) | — |
| `use` | `Player::interact` | `Tile::use`, senão `ItemInstance::useOn` | `ItemInstance::use` |

— então abrir porta, apertar botão, abrir baú/fornalha/bancada, acender
fogo, arar terra, encher balde, plantar muda, montar cavalo, tosquiar ovelha,
domesticar lobo, comer, beber poção e puxar o arco **já funcionam sem código
específico**, porque o motor implementa cada um dentro dessas primitivas.
Qualquer mecânica que o motor ganhe no futuro vale para o agente de graça.

Além disso: mira por raycast (o mesmo de `GameRenderer::pick`, entidade tem
prioridade sobre bloco), pitch além de yaw, agachar/correr, seleção de slot
da hotbar, troca de slots, soltar item/pilha/inventário,
soltar/cancelar item em uso, fechar container — e as checagens do sistema de
confiança (`isAllowedToMine`, `isAllowedToHurtEntity`, ...) preservadas, para
o bot não ter poderes que nenhum jogador tem.

Observações: posição, rotação, velocidade, água/lava, agachado, correndo,
dormindo, usando item, vivo, vida, fome, saturação, ar, XP, hora do dia, luz,
bioma, alvo mirado, blocos vizinhos, entidade mais próxima, inventário
completo, container aberto e o resultado da última ação.

Verificado por `tests/test_player_parity.py` em três camadas (campo existe no
protocolo / é lido pelo C++ / o C++ usa a primitiva certa do motor). Ver
`README.md`, seção "Paridade de mecânicas com o Player".

### Etapa 2 — Curiosidade por região nunca visitada ✅
`curiosity.CountBasedCuriosity`: recompensa `1/sqrt(N)` por visitas ao chunk,
mais bônus pontuais por **novidade categórica** (bioma novo, tipo de bloco
novo) — que é o que faz o agente demonstrar interesse por novidades e não só
por coordenadas.

### Etapa 3 — PPO ✅ (implementado; falta rodar o treino longo)
`env.CuriousMobEnv` é um `gymnasium.Env` sobre a ponte, com espaço de ação
discreto (`ACTIONS`) e observação de 22 features normalizadas
(`curiosity.encode_state`). `train.py` treina com Stable-Baselines3 e
persiste modelo + memória, inclusive se interrompido.

Limitação honesta: o Minecraft não rebobina, então `reset()` só reconecta.
Episódios terminam por tempo ou morte — `truncated` é o caminho normal.

### Etapa 4 — Random Network Distillation ✅
`curiosity.RNDCuriosity`: rede-alvo aleatória congelada + preditor treinado
online, com normalização Welford do erro (a escala do erro cai ordens de
grandeza durante o treino, e recompensa de escala variável desestabiliza o
PPO). `CombinedCuriosity` soma com a contagem. Sem torch, `build_curiosity`
degrada para contagem em vez de falhar.

### Etapa 5 — Memória de longo prazo ✅
`memory.Memory`, indexada por chunk: visitas, primeiro/último tick, dano
sofrido (de onde emerge "área perigosa", sem ninguém rotular nada), bioma e
histograma de blocos vistos. Consultas: `favourite_chunks`,
`dangerous_chunks`, `frontier_chunks`, `is_looping`. Persistida em JSON e
recarregada entre sessões — sem isso, todo restart faria o mundo parecer novo.

### Etapa 6 — Objetivos emergentes (em aberto, por definição)
Explorar cavernas, subir montanhas, seguir rios, visitar aldeias, colecionar
blocos — não programados diretamente, observados como comportamento
emergente. Nada a implementar: a recompensa é deliberadamente **só**
curiosidade + sobrevivência, sem nenhum termo de tarefa (`env._reward`), e é
essa ausência que dá espaço para a emergência. O que falta é rodar e observar.

### O que falta

- **Rodar o treino de verdade.** `train.py` está pronto, mas 100k passos são
  horas de jogo ao vivo; nada disso foi executado.
- **Crafting e drag-and-drop de slots** dentro de um container aberto. O
  container é observável e abrir/fechar funciona, mas manipular os slots
  exigiria replicar `AbstractContainerMenu::clicked` fora do menu.
- **Mapa id -> nome** de blocos e itens (hoje trafegam só ids numéricos).
- **Validação em runtime** das mecânicas dentro do mundo: os testes provam
  que o caminho existe e está ligado, não que funciona com o jogo aberto.
  Roteiro manual no `README.md`.

## Ideias futuras (preservadas do plano original)
- múltiplos bots aprendendo juntos;
- comunicação entre bots;
- troca de conhecimento;
- personalidade própria / emoções simuladas;
- construção automática;
- curiosidade social;
- linguagem emergente.

## Critérios de sucesso (sem mudança)
- o agente explora espontaneamente;
- evita repetir trajetos sem necessidade;
- demonstra preferência por novidades;
- apresenta comportamentos não programados diretamente;
- continua aprendendo após muitas horas de jogo.
