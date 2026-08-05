"""Cliente de ponte do CuriousMob.

Conecta no servidor TCP aberto pelo jogo (CuriousMobBridge), lê uma linha de
estado por vez, decide uma ação (por enquanto ALEATÓRIA, só para validar o
round-trip ponta a ponta) e envia a ação de volta.

Uso:
    python mods/CuriousMob/ai/environment.py

Este arquivo é o MVP. A política real (sobrevivência, depois curiosidade/RND)
entra em policy.py numa próxima etapa — ver mods/CuriousMob/PLANO.md.
"""

from __future__ import annotations

import random
import socket
import sys
from pathlib import Path
from typing import BinaryIO, Iterator

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from protocol.messages import Action, State  # noqa: E402

HOST = "127.0.0.1"
PORT = 5555

MOVE_CHOICES = ["forward", "back", "left", "right", "none"]
TURN_CHOICES = ["left", "right", "none"]


def random_action() -> Action:
    return Action(
        move=random.choice(MOVE_CHOICES),
        turn=random.choice(TURN_CHOICES),
        jump=random.random() < 0.1,
        break_block=random.random() < 0.05,
        place_block=False,
        attack=False,
    )


def iter_states(buf: BinaryIO) -> Iterator[State]:
    """Gera um State por vez a partir do socket, sem acumular nada em lista.

    O jogo roda indefinidamente (uma linha por tick de estado enviado), então
    qualquer coisa que junte tudo numa lista antes de processar cresceria sem
    limite. `buf` (arquivo do socket) já é iterável linha a linha de forma
    preguiçosa — este gerador só soma o parse/filtro de linha vazia em cima
    disso, mantendo o uso de RAM limitado a um State por vez.
    """
    for raw_line in buf:
        line = raw_line.decode("utf-8").strip()
        if not line:
            continue
        yield State.from_json(line)


def main() -> None:
    with socket.create_connection((HOST, PORT)) as sock:
        print(f"Conectado ao CuriousMobBridge em {HOST}:{PORT}")
        buf = sock.makefile("rwb", buffering=0)
        for state in iter_states(buf):
            action = random_action()
            print(f"tick={state.tick} pos=({state.x:.1f},{state.y:.1f},{state.z:.1f}) -> {action}")
            buf.write((action.to_json() + "\n").encode("utf-8"))


if __name__ == "__main__":
    main()
