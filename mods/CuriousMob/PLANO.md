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

### Etapa 1 (implementada nesta rodada)
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

### Não implementado ainda (próximas etapas)
- Abrir containers/portas, uso de itens além de quebrar/colocar bloco,
  inventário completo (crafting, drag-and-drop de slots).
- Observações mais ricas (bioma, blocos vizinhos, entidade mais próxima,
  horário do dia, inventário) — hoje o estado é reduzido (posição, rotação,
  chão, vida, fome, bloco à frente).
- Sistema de curiosidade real (contagem de visitas por região → depois RND).
- Memória espacial (chunks visitados, blocos raros, biomas conhecidos).
- Treinamento real (PPO via Stable-Baselines3) — hoje a política é aleatória,
  só para validar o round-trip da ponte.

## Roadmap original (preservado como visão de longo prazo)

### Etapa 2 — Curiosidade baseada em região nunca visitada
Cada chunk/região visitada recebe um contador; quanto menos visitada, maior a
recompensa. Implementado do lado Python (`curiosity.py`), usando o estado de
posição já enviado pela ponte.

### Etapa 3 — Treinar PPO
Ambiente Gymnasium formal em cima do protocolo (`protocol/messages.py`),
treinado com Stable-Baselines3, ainda sem rede de curiosidade.

### Etapa 4 — Random Network Distillation (RND)
Recompensa por estados difíceis de prever, como aproximação de curiosidade
intrínseca.

### Etapa 5 — Memória de longo prazo
Locais favoritos, áreas perigosas, caminhos conhecidos.

### Etapa 6 — Objetivos emergentes
Explorar cavernas, subir montanhas, seguir rios, visitar aldeias, colecionar
blocos — não programados diretamente, observados como comportamento
emergente.

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
