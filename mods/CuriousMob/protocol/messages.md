# Protocolo CuriousMob (C++ <-> Python)

Transporte: TCP, o jogo (`4jcraft`) atua como **servidor** em `127.0.0.1:5555` (configurável).
Framing: uma mensagem JSON por linha (`\n`-delimited), UTF-8.

A referência executável deste documento é `protocol/messages.py` (dataclasses
`State`/`Action`) e os testes em `tests/test_protocol.py` e
`tests/test_cpp_json.py` — este último compila o serializador C++ e confere
os dois sentidos do round-trip.

## Estado (jogo -> Python)

Enviado a cada N ticks (padrão: a cada 5 ticks, ~4x por segundo a 20 TPS).
Montado por `CuriousMobController::buildStateJson`.

```json
{
  "tick": 12345,

  "x": 10.5, "y": 64.0, "z": -3.25,
  "yaw": 90.0, "pitch": -10.0,
  "vx": 0.1, "vy": -0.08, "vz": 0.0,

  "on_ground": true, "in_water": false, "in_lava": false,
  "sneaking": false, "sprinting": true, "sleeping": false,
  "using_item": false, "alive": true,

  "health": 18.0, "max_health": 20.0,
  "food": 15, "saturation": 2.5,
  "air": 300, "xp_level": 3, "xp_progress": 0.4,

  "day_time": 1200, "is_day": true, "light": 15.0,
  "biome": { "id": 4, "name": "Forest" },

  "block_in_front": 3,

  "target": {
    "type": "tile", "x": 13, "y": 64, "z": -3, "face": 1,
    "block": 2, "data": 0, "distance": 1.8
  },

  "blocks": {
    "feet": 0, "head": 0, "below": 2, "above": 0,
    "north": 0, "south": 3, "west": 0, "east": 1
  },

  "nearest_entity": {
    "type": 7, "name": "Cow",
    "dx": 3.0, "dy": 0.0, "dz": 4.0, "distance": 5.0, "health": 10.0
  },

  "inventory": {
    "selected": 2,
    "items": [{ "slot": 2, "id": 278, "count": 1, "aux": 5 }],
    "armor": [{ "slot": 3, "id": 306, "count": 1, "aux": 0 }],
    "held":  { "slot": 2, "id": 278, "count": 1, "aux": 5 }
  },

  "container": {
    "size": 27,
    "items": [{ "slot": 0, "id": 264, "count": 3, "aux": 0 }]
  },

  "last_result": { "attack": true, "use": false, "break": true, "place": false }
}
```

### Notas sobre os campos

- **`target`** é o análogo do `hitResult` do jogador: o resultado do mesmo
  raycast que `GameRenderer::pick` faz para a mira do humano, com entidade
  tendo prioridade sobre bloco quando está mais perto. `type` é
  `"tile" | "entity" | "none"`. Para `"entity"`, os campos são
  `entity_type` (o `eINSTANCEOF` do motor), `entity_name`, `entity_health`
  e `distance`.
- **`blocks`** é a vizinhança imediata da célula do bot, para a política
  perceber "parede à frente"/"buraco abaixo" sem depender do raycast, que só
  devolve um alvo.
- **`inventory.items`/`armor`/`container.items`** listam **apenas slots
  ocupados** — um inventário vazio por extenso seriam 36 objetos `null` por
  mensagem, 4x por segundo, sem informação.
- **`container`** é `null` quando nenhum container está aberto.
- **`last_result`** diz se a ação do tick anterior teve efeito. É o sinal de
  recompensa direto para o agente ("o ataque acertou?", "o `use` fez algo?"),
  sem precisar inferir de mudança de posição. A chave é `"break"` no fio; no
  dataclass Python é `break_` (palavra reservada).
- **`block_in_front`** é legado da Etapa 1, mantido para não quebrar clientes
  antigos. Use `target` no lugar.
- Ids de bloco/item são numéricos (0 = ar, -1 = indisponível), não nomes.
  Mapear id -> nome continua fora de escopo.
- `NaN`/`infinito` nunca são emitidos: o serializador os converte para `0.0`,
  porque não são JSON válido e derrubariam o `json.loads` do agente no meio
  de uma sessão.

### Compatibilidade

`State.from_json` é **tolerante**: campo ausente cai no default, campo
desconhecido é ignorado. O jogo e o processo Python são compilados/rodados
separadamente, então é normal que um lado esteja à frente do outro durante o
desenvolvimento — e um `TypeError` por campo novo derrubaria uma sessão
longa de treino.

## Ação (Python -> jogo)

Enviada em resposta a cada estado recebido (ou a qualquer momento; a última
ação recebida é a que vale no próximo tick do bot). Todos os campos são
opcionais — o que não vier fica no default.

```json
{
  "move": "forward",
  "forward": 1.0, "strafe": 0.0,
  "jump": false, "sneak": false, "sprint": true,

  "turn": -12.5, "look_pitch": 5.0,

  "attack": false,
  "use": false,

  "break_block": false,
  "place_block": false,

  "select_slot": 3,
  "swap_from": 0, "swap_to": 8,
  "drop": false, "drop_stack": false, "drop_all": false,

  "release_item": false,
  "stop_item": false,
  "close_container": false
}
```

### Locomoção

- `move`: `"forward" | "back" | "left" | "right" | "none"`.
- `forward`/`strafe`: contínuos em `-1..1`. **Se presentes, vencem `move`** —
  uma política discreta usa `move`, uma contínua produz os dois floats
  naturalmente.
- `jump`, `sneak`, `sprint`: booleanos, aplicados no tick.

### Mira

- `turn`: delta de yaw em **graus**. Também aceita `"left"`/`"right"` como
  atalho categórico (passo fixo de 6°, comportamento da Etapa 1).
- `look_pitch`: delta de pitch em graus, com o mesmo clamp de -90..90 que o
  input do jogador real.

### Cliques do mouse

`attack` e `use` são os dois botões do mouse, roteados pelo alvo mirado
exatamente como o jogo roteia os do jogador:

| | alvo é entidade | alvo é bloco | nada na mira |
|---|---|---|---|
| `attack` | `Player::attack` | quebra o bloco (com desgaste de ferramenta e drops) | nada |
| `use` | `Player::interact` | `Tile::use`, e se o bloco não consumir, `ItemInstance::useOn` | `ItemInstance::use` |

É por isso que **não há uma ação por mecânica**: abrir porta, apertar botão,
abrir baú, acender fogo com isqueiro, arar terra com enxada, encher balde,
plantar muda, montar cavalo, tosquiar ovelha, domesticar lobo, comer, beber
poção e puxar o arco são todos `use` — o jogo já implementa cada um dentro
de `Tile::use`/`Item::useOn`/`Item::use`, e o bot passa pelo mesmo caminho.

`break_block`/`place_block` são atalhos determinísticos da Etapa 1, mantidos
por compatibilidade: agem sobre o alvo mirado, e `place_block` pula
`Tile::use` de propósito para não ter o clique consumido por um baú/porta
atrás da mira.

### Inventário

- `select_slot`: `0..8` (hotbar). É aplicado **antes** dos cliques do mesmo
  tick, como um jogador rolando a hotbar antes de clicar.
- `swap_from`/`swap_to`: move item entre slots (só tem efeito se os dois
  vierem juntos).
- `drop` (1 item), `drop_stack` (a pilha), `drop_all` (o inventário inteiro).

### Item em uso e container

- `release_item`: solta a corda do arco / termina de comer
  (`Player::releaseUsingItem`).
- `stop_item`: cancela o uso (`Player::stopUsingItem`).
- `close_container`: fecha o container aberto. **Obrigatório** depois de
  abrir um: com um container aberto o agente fica num estado em que boa
  parte do input é ignorada.

### Ausente ≠ presente

Campos ausentes significam "não mexe"; campos presentes são aplicados. Por
isso `Action.to_json` **omite campos `None`** — mandar `"select_slot": null`
a cada tick reescreveria o slot selecionado para o fallback. `select_slot`,
`swap_from` e `swap_to` são os campos em que essa distinção importa.

Booleanos aceitam `true`/`false` **ou** `0`/`1`, porque uma política com
espaço de ação discreto serializa flags como int sem que isso seja um erro.

## Fora de escopo

- Crafting e drag-and-drop de slots dentro de um container aberto (mover
  itens entre o container e o inventário). O container é **observável**
  (`state.container`) e abrir/fechar funciona, mas manipular os slots dele
  exigiria replicar `AbstractContainerMenu::clicked` fora do menu.
- Mapear id numérico de bloco/item para nome.
- Chat e comandos: o sistema de comandos deste projeto é um registro fechado
  (`EGameCommand`) disparado só por pacote de rede, sem parser de texto livre.
