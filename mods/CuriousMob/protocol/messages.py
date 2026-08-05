"""(De)serialização das mensagens do protocolo CuriousMob.

Espelha mods/CuriousMob/protocol/messages.md. Uma mensagem por linha, JSON,
trafegada em um socket TCP newline-delimited.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field


@dataclass
class State:
    tick: int
    x: float
    y: float
    z: float
    yaw: float
    pitch: float
    on_ground: bool
    health: float
    food: int
    block_in_front: int  # id numérico do tile (0 = ar, -1 = indisponível)

    @staticmethod
    def from_json(line: str) -> "State":
        data = json.loads(line)
        return State(**data)


@dataclass
class Action:
    move: str = "none"          # forward | back | left | right | none
    turn: str = "none"          # left | right | none
    jump: bool = False
    break_block: bool = False
    place_block: bool = False
    attack: bool = False

    def to_json(self) -> str:
        return json.dumps(asdict(self))
