"""Cliente de ponte do CuriousMob.

Conecta no servidor TCP aberto pelo jogo (CuriousMobBridge), lê uma linha de
estado por vez, decide uma ação e a envia de volta.

Por padrão usa `policy.CuriousPolicy` (curiosidade + sobrevivência, sem
treino). `--random` volta à política aleatória da Etapa 1, útil para
diagnosticar a ponte isoladamente da política. `--model` carrega um PPO
treinado por `train.py`.

Uso:
    python mods/CuriousMob/ai/environment.py
    python mods/CuriousMob/ai/environment.py --random
    python mods/CuriousMob/ai/environment.py --model mods/CuriousMob/models/ppo_curiousmob.zip

Ver mods/CuriousMob/PLANO.md.
"""

from __future__ import annotations

import argparse
import random
import signal
import socket
import sys
from pathlib import Path
from typing import BinaryIO, Iterator, Protocol

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from protocol.messages import Action, State  # noqa: E402

from curiosity import build_curiosity  # noqa: E402
from memory import Memory  # noqa: E402
from policy import CuriousPolicy  # noqa: E402

HOST = "127.0.0.1"
PORT = 5555

DEFAULT_MEMORY = Path(__file__).resolve().parent.parent / "models" / "memory.json"

MOVE_CHOICES = ["forward", "back", "left", "right", "none"]
TURN_CHOICES = ["left", "right", "none"]

# Loga só 1 a cada N estados: em uma sessão longa o bot roda indefinidamente
# enviando estado várias vezes por segundo, e imprimir cada um enche o
# scrollback do terminal (RAM do emulador de terminal) sem necessidade.
LOG_EVERY = 100

# De quantos em quantos estados gravar a memória em disco. Uma sessão de
# treino de horas não pode perder tudo se o jogo fechar.
SAVE_EVERY = 2000


class Policy(Protocol):
    def decide(self, state: State) -> Action: ...


class RandomPolicy:
    """Política da Etapa 1, mantida para diagnosticar a ponte."""

    def __init__(self, rng: random.Random | None = None) -> None:
        self.rng = rng or random.Random()

    def decide(self, state: State) -> Action:
        return Action(
            move=self.rng.choice(MOVE_CHOICES),
            turn=self.rng.choice(TURN_CHOICES),
            jump=self.rng.random() < 0.1,
            attack=self.rng.random() < 0.05,
            use=self.rng.random() < 0.05,
        )


def iter_states(buf: BinaryIO) -> Iterator[State]:
    """Gera um State por vez a partir do socket, sem acumular nada em lista.

    O jogo roda indefinidamente (uma linha por tick de estado enviado), então
    qualquer coisa que junte tudo numa lista antes de processar cresceria sem
    limite. `buf` (arquivo do socket) já é iterável linha a linha de forma
    preguiçosa — este gerador só soma o parse/filtro de linha vazia em cima
    disso, mantendo o uso de RAM limitado a um State por vez.

    Linhas malformadas são puladas com aviso em vez de derrubar o agente: um
    JSON truncado (jogo fechando no meio de um write) não é motivo para
    perder uma sessão inteira de exploração.
    """
    import json

    for raw_line in buf:
        line = raw_line.decode("utf-8", errors="replace").strip()
        if not line:
            continue
        try:
            yield State.from_json(line)
        except (json.JSONDecodeError, TypeError, ValueError) as exc:
            print(f"[aviso] estado ignorado ({exc}): {line[:120]}", file=sys.stderr)


def build_policy(args: argparse.Namespace, memory: Memory) -> Policy:
    if args.random:
        return RandomPolicy()
    if args.model is not None:
        from policy import TrainedPolicy

        return TrainedPolicy(args.model)
    return CuriousPolicy(memory)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Cliente Python do CuriousMob")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument(
        "--random", action="store_true", help="política aleatória (diagnóstico)"
    )
    parser.add_argument(
        "--model", type=Path, default=None, help="modelo PPO salvo por train.py"
    )
    parser.add_argument("--memory", type=Path, default=DEFAULT_MEMORY)
    parser.add_argument(
        "--no-memory-save", action="store_true", help="não gravar a memória em disco"
    )
    parser.add_argument("--rnd", action="store_true", help="somar curiosidade RND")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    memory = Memory() if args.no_memory_save else Memory.load(args.memory)
    curiosity = build_curiosity(memory, use_rnd=args.rnd)
    policy = build_policy(args, memory)

    def save() -> None:
        if not args.no_memory_save:
            memory.save(args.memory)

    # Ctrl-C durante uma sessão longa não pode custar a memória acumulada.
    signal.signal(signal.SIGINT, lambda *_: (save(), sys.exit(0)))

    print(
        f"Conectando em {args.host}:{args.port} "
        f"(política: {type(policy).__name__}, {len(memory.chunks)} chunks conhecidos)"
    )

    try:
        with socket.create_connection((args.host, args.port)) as sock:
            print("Conectado ao CuriousMobBridge")
            buf = sock.makefile("rwb", buffering=0)
            seen = 0
            for state in iter_states(buf):
                memory.visit(state)
                novelty = curiosity.reward(state)
                action = policy.decide(state)
                buf.write((action.to_json() + "\n").encode("utf-8"))

                seen += 1
                if state.tick % LOG_EVERY == 0:
                    biome = state.biome.name if state.biome else "?"
                    print(
                        f"tick={state.tick} "
                        f"pos=({state.x:.1f},{state.y:.1f},{state.z:.1f}) "
                        f"bioma={biome} vida={state.health:.0f} fome={state.food} "
                        f"novidade={novelty:.2f} chunks={len(memory.chunks)} "
                        f"-> {_describe(action)}"
                    )
                if seen % SAVE_EVERY == 0:
                    save()
    except ConnectionRefusedError:
        print(
            f"Nada escutando em {args.host}:{args.port}. O jogo está rodando com "
            "CURIOUSMOB_SPAWN=1?",
            file=sys.stderr,
        )
        return 1
    finally:
        save()

    return 0


def _describe(action: Action) -> str:
    """Só os campos ativos — o dataclass inteiro polui demais o log."""
    parts = []
    if action.move != "none":
        parts.append(action.move)
    if action.turn not in ("none", 0.0):
        parts.append(f"turn={action.turn}")
    for flag in ("jump", "sneak", "sprint", "attack", "use", "drop", "close_container"):
        if getattr(action, flag):
            parts.append(flag)
    if action.select_slot is not None:
        parts.append(f"slot={action.select_slot}")
    return " ".join(parts) or "idle"


if __name__ == "__main__":
    raise SystemExit(main())
