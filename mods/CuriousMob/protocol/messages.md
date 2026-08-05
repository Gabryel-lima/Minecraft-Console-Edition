# Protocolo CuriousMob (C++ <-> Python)

Transporte: TCP, o jogo (`4jcraft`) atua como **servidor** em `127.0.0.1:5555` (configurável).
Framing: uma mensagem JSON por linha (`\n`-delimited), UTF-8.

## Estado (jogo -> Python)

Enviado a cada N ticks (padrão: a cada 5 ticks, ~4x por segundo a 20 TPS).

```json
{
  "tick": 12345,
  "x": 10.5, "y": 64.0, "z": -3.25,
  "yaw": 90.0, "pitch": 0.0,
  "on_ground": true,
  "health": 20.0,
  "food": 18,
  "block_in_front": 1
}
```

`block_in_front` é o id numérico do tile (0 = ar, -1 = indisponível), não o
nome — mapear id -> nome fica para uma próxima etapa.

Campos reservados para uma próxima etapa (não enviados no MVP): bioma, blocos
laterais/abaixo/acima, entidade mais próxima, horário do dia, inventário.

## Ação (Python -> jogo)

Enviada em resposta a cada estado recebido (ou a qualquer momento; a última ação
recebida é a que vale no próximo tick do bot).

```json
{
  "move": "forward",
  "turn": "none",
  "jump": false,
  "break_block": false,
  "place_block": false,
  "attack": false
}
```

- `move`: um de `"forward" | "back" | "left" | "right" | "none"`.
- `turn`: um de `"left" | "right" | "none"` (gira a cabeça/corpo do bot).
- `jump`, `break_block`, `place_block`, `attack`: booleanos, aplicados no tick em
  que a ação é recebida.
- `break_block`/`place_block` agem sobre o bloco na célula à frente do bot
  (posição do bot + vetor de direção do olhar).
- `attack` mira a entidade viva mais próxima dentro de um raio curto.

Ações não reconhecidas ou ausentes equivalem a "nenhuma ação" nesse campo.

## Fora de escopo no MVP

Abrir containers/portas, uso de itens além de quebrar/colocar bloco, e
inventário completo (seleção de slot, crafting) ficam para uma etapa futura —
ver `mods/CuriousMob/PLANO.md`.
