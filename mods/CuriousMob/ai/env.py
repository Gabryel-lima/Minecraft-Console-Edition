"""Ambiente Gymnasium sobre a ponte CuriousMob — Etapa 3 do PLANO.md.

Envolve o socket TCP do jogo num `gymnasium.Env` para que qualquer algoritmo
de RL padrão (PPO do Stable-Baselines3, no nosso caso) possa treinar o agente
sem conhecer o protocolo.

Decisões de projeto que valem registro:

- **Espaço de ação discreto.** O input real de um jogador é um híbrido
  (contínuo na mira, discreto nos cliques). Achatamos isso numa lista fixa de
  ações compostas (`ACTIONS`), cada uma um lambda que devolve uma `Action`
  completa. É o que torna PPO com MlpPolicy aplicável direto, e mantém uma
  correspondência 1:1 legível entre índice e comportamento.

- **Sem `reset()` de verdade.** O mundo do Minecraft não rebobina: não existe
  "reiniciar o episódio" do lado do jogo. `reset()` só reconecta e devolve a
  primeira observação; os episódios são cortados por tempo
  (`max_episode_steps`) ou pela morte do bot. É a limitação honesta de treinar
  num jogo ao vivo, e é por isso que `truncated` é o caminho normal de fim de
  episódio, não `terminated`.

- **Recompensa = curiosidade + sobrevivência.** Nenhum objetivo de tarefa é
  codificado (não há "+10 por minerar diamante"). Isso é deliberado: o
  critério de sucesso do PLANO.md é comportamento emergente, e recompensar
  tarefas específicas produziria o oposto.
"""

from __future__ import annotations

import socket
import sys
from pathlib import Path
from typing import Any, Callable, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from protocol.messages import Action, State  # noqa: E402

from curiosity import STATE_DIM, build_curiosity, encode_state  # noqa: E402
from memory import Memory  # noqa: E402

HOST = "127.0.0.1"
PORT = 5555

# Ações compostas. A ordem é o espaço de ação — mudar a ordem invalida
# modelos já treinados, então acrescente no fim, nunca no meio.
ACTIONS: list[Callable[[], Action]] = [
    lambda: Action(move="forward"),
    lambda: Action(move="forward", sprint=True),
    lambda: Action(move="back"),
    lambda: Action(move="left"),
    lambda: Action(move="right"),
    lambda: Action(move="forward", jump=True),
    lambda: Action(turn=-15.0),
    lambda: Action(turn=15.0),
    lambda: Action(look_pitch=-15.0),
    lambda: Action(look_pitch=15.0),
    lambda: Action(attack=True),
    lambda: Action(use=True),
    lambda: Action(move="forward", sneak=True),
    lambda: Action(drop=True),
    lambda: Action(close_container=True),
    lambda: Action(),  # não fazer nada é uma escolha legítima
]

encode_observation = encode_state


def _reward(state: State, previous: Optional[State], novelty: float) -> float:
    """Curiosidade + termos de sobrevivência, sem objetivo de tarefa."""
    reward = novelty

    if previous is not None:
        # Perder vida dói; recuperar vale o mesmo em positivo.
        reward += (state.health - previous.health) * 0.5
        # Fome caindo é um custo suave — desincentiva correr sem parar.
        reward += (state.food - previous.food) * 0.1

    if not state.alive:
        reward -= 50.0
    if state.in_lava:
        reward -= 5.0
    if state.in_water and state.air < 100:
        reward -= 1.0

    return reward


class CuriousMobEnv:
    """`gymnasium.Env` sobre a ponte. Importa Gymnasium sob demanda.

    O import de `gymnasium` fica dentro de `__init__` de propósito: o resto
    do pacote (`policy`, `memory`, `curiosity`, `environment`) roda com a
    biblioteca padrão, e não queremos que um `import env` para inspecionar
    `ACTIONS` exija a dependência inteira.
    """

    metadata = {"render_modes": []}

    def __init__(
        self,
        host: str = HOST,
        port: int = PORT,
        max_episode_steps: int = 2000,
        use_rnd: bool = False,
        memory: Optional[Memory] = None,
    ) -> None:
        import gymnasium as gym
        import numpy as np

        self._np = np
        self.action_space = gym.spaces.Discrete(len(ACTIONS))
        self.observation_space = gym.spaces.Box(
            low=-10.0, high=10.0, shape=(STATE_DIM,), dtype=np.float32
        )

        self.host = host
        self.port = port
        self.max_episode_steps = max_episode_steps

        self.memory = memory or Memory()
        self.curiosity = build_curiosity(self.memory, use_rnd=use_rnd)

        self._sock: Optional[socket.socket] = None
        self._buf: Any = None
        self._previous: Optional[State] = None
        self._steps = 0

    # -- ciclo de vida da conexão --

    def _connect(self) -> None:
        self.close()
        self._sock = socket.create_connection((self.host, self.port))
        self._buf = self._sock.makefile("rwb", buffering=0)

    def close(self) -> None:
        if self._buf is not None:
            try:
                self._buf.close()
            except OSError:
                pass
            self._buf = None
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def _read_state(self) -> State:
        if self._buf is None:
            raise RuntimeError("env não conectado — chame reset() primeiro")
        for raw_line in self._buf:
            line = raw_line.decode("utf-8").strip()
            if line:
                return State.from_json(line)
        raise ConnectionError("ponte CuriousMob fechou a conexão")

    def _send(self, action: Action) -> None:
        if self._buf is None:
            raise RuntimeError("env não conectado — chame reset() primeiro")
        self._buf.write((action.to_json() + "\n").encode("utf-8"))

    # -- API Gymnasium --

    def reset(self, *, seed: Optional[int] = None, options: Optional[dict] = None):
        del seed, options  # o mundo do jogo não é semeável a partir daqui
        self._connect()
        self._steps = 0
        state = self._read_state()
        self.memory.visit(state)
        self._previous = state
        return self._np.array(encode_state(state), dtype=self._np.float32), {}

    def step(self, action_index: int):
        self._send(ACTIONS[int(action_index)]())
        state = self._read_state()

        self.memory.visit(state)
        novelty = self.curiosity.reward(state)
        reward = _reward(state, self._previous, novelty)

        self._steps += 1
        terminated = not state.alive
        truncated = self._steps >= self.max_episode_steps

        info = {
            "tick": state.tick,
            "novelty": novelty,
            "chunks_known": len(self.memory.chunks),
            "biomes_seen": len(self.memory.biomes_seen),
        }
        self._previous = state
        obs = self._np.array(encode_state(state), dtype=self._np.float32)
        return obs, float(reward), terminated, truncated, info
