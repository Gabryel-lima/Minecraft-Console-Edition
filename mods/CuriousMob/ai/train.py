"""Treino PPO do CuriousMob — Etapas 3 e 4 do PLANO.md.

Pré-requisitos: o jogo rodando com `CURIOUSMOB_SPAWN=1` (a ponte precisa
estar escutando) e as dependências de treino instaladas:

    pip install -r mods/CuriousMob/ai/requirements-train.txt

Uso:

    python mods/CuriousMob/ai/train.py --steps 100000 --rnd
    python mods/CuriousMob/ai/train.py --resume models/ppo_curiousmob.zip

O treino é *online contra o jogo ao vivo*: cada passo de PPO é um tick real
do Minecraft, então 100k passos levam horas de relógio. Não há paralelismo
de ambientes — há um bot só no mundo. Se quiser acelerar, o caminho é
diminuir `STATE_SEND_INTERVAL` no C++ (mais decisões por segundo), não somar
processos.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from env import CuriousMobEnv  # noqa: E402
from memory import Memory  # noqa: E402

DEFAULT_MODEL = Path(__file__).resolve().parent.parent / "models" / "ppo_curiousmob.zip"
DEFAULT_MEMORY = Path(__file__).resolve().parent.parent / "models" / "memory.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Treina o CuriousMob com PPO")
    parser.add_argument("--steps", type=int, default=100_000, help="passos de treino")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--rnd", action="store_true", help="somar curiosidade RND")
    parser.add_argument(
        "--episode-steps", type=int, default=2000, help="passos por episódio"
    )
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--memory", type=Path, default=DEFAULT_MEMORY)
    parser.add_argument(
        "--resume", type=Path, default=None, help="continuar de um modelo salvo"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        from stable_baselines3 import PPO
    except ImportError:
        print(
            "stable-baselines3 não está instalado.\n"
            "  pip install -r mods/CuriousMob/ai/requirements-train.txt",
            file=sys.stderr,
        )
        return 1

    # A memória sobrevive entre execuções: a curiosidade por contagem só faz
    # sentido se "já visitei este chunk" persistir de uma sessão para a
    # outra. Sem isso, todo restart faz o mundo inteiro parecer novo.
    memory = Memory.load(args.memory)
    print(f"Memória carregada: {len(memory.chunks)} chunks conhecidos")

    env = CuriousMobEnv(
        host=args.host,
        port=args.port,
        max_episode_steps=args.episode_steps,
        use_rnd=args.rnd,
        memory=memory,
    )

    if args.resume is not None and args.resume.exists():
        print(f"Retomando de {args.resume}")
        model = PPO.load(str(args.resume), env=env)
    else:
        model = PPO("MlpPolicy", env, verbose=1, n_steps=512, batch_size=64)

    try:
        model.learn(total_timesteps=args.steps)
    except (ConnectionError, KeyboardInterrupt) as exc:
        # Interromper o treino é normal (o jogo fecha, você aperta Ctrl-C).
        # O que não pode acontecer é perder o modelo e a memória por isso.
        print(f"\nTreino interrompido: {exc}", file=sys.stderr)
    finally:
        args.model.parent.mkdir(parents=True, exist_ok=True)
        model.save(str(args.model))
        memory.save(args.memory)
        env.close()
        print(f"Modelo salvo em {args.model}")
        print(f"Memória salva em {args.memory} ({len(memory.chunks)} chunks)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
