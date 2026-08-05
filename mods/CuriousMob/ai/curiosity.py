"""Curiosidade intrínseca — Etapas 2 e 4 do PLANO.md.

Duas implementações, mesma interface (`reward(state) -> float`):

- `CountBasedCuriosity` (Etapa 2): recompensa 1/sqrt(N) por região visitada,
  onde N é o número de visitas ao chunk. É barato, não tem hiperparâmetro
  difícil e já produz exploração dirigida. Também soma bônus pontuais por
  novidade *categórica* (bioma novo, tipo de bloco novo), que é o que faz o
  agente "demonstrar interesse por novidades" e não só por coordenadas.

- `RNDCuriosity` (Etapa 4): Random Network Distillation. Uma rede-alvo
  aleatória fixa e uma rede preditora treinada para imitá-la; o erro de
  predição é a recompensa. Estados que o preditor ainda não domina são, por
  construção, estados pouco vistos. Requer PyTorch — se não estiver
  instalado, `RNDCuriosity` levanta ImportError na construção e o chamador
  deve cair para a versão por contagem.

Por que as duas: a versão por contagem satura num mundo grande (todo chunk
novo vale o mesmo), enquanto RND continua discriminando dentro de um chunk já
visitado. `CombinedCuriosity` soma as duas com pesos.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import Protocol

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from protocol.messages import State  # noqa: E402

from memory import Memory  # noqa: E402

# Bônus somados uma única vez, quando a categoria é vista pela primeira vez.
NEW_BIOME_BONUS = 5.0
NEW_BLOCK_BONUS = 1.0


class Curiosity(Protocol):
    """Interface comum: transforma um estado numa recompensa de novidade."""

    def reward(self, state: State) -> float: ...


class CountBasedCuriosity:
    """Recompensa por região pouco visitada + bônus por categoria nova.

    A memória é compartilhada com a política (mesmo objeto `Memory`), então
    `reward()` NÃO chama `memory.visit()`: quem tem a responsabilidade de
    registrar a visita é o laço principal, uma vez por estado. Chamar aqui
    também contaria cada estado duas vezes.
    """

    def __init__(self, memory: Memory, scale: float = 1.0) -> None:
        self.memory = memory
        self.scale = scale
        self._biomes_rewarded: set[str] = set()
        self._blocks_rewarded: set[int] = set()

    def reward(self, state: State) -> float:
        visits = max(1, self.memory.visits(state.chunk()))
        bonus = self.scale / math.sqrt(visits)

        if state.biome is not None and state.biome.name:
            if state.biome.name not in self._biomes_rewarded:
                self._biomes_rewarded.add(state.biome.name)
                bonus += NEW_BIOME_BONUS

        for block_id in Memory.observed_blocks(state):
            if block_id not in self._blocks_rewarded:
                self._blocks_rewarded.add(block_id)
                bonus += NEW_BLOCK_BONUS

        return bonus


def encode_state(state: State) -> list[float]:
    """Vetor de features normalizado — a entrada da RND e da política PPO.

    Normalizado para ~[-1, 1] porque redes pequenas com entrada em escalas
    muito diferentes (y em centenas, health em dezenas) treinam mal.
    As posições x/z entram como seno/cosseno de escala grossa em vez de
    valor cru: o mundo é praticamente ilimitado e um x=30000 dominaria todo
    o resto do vetor.
    """
    period = 256.0
    return [
        math.sin(state.x / period * math.tau),
        math.cos(state.x / period * math.tau),
        math.sin(state.z / period * math.tau),
        math.cos(state.z / period * math.tau),
        state.y / 128.0 - 1.0,
        math.sin(math.radians(state.yaw)),
        math.cos(math.radians(state.yaw)),
        state.pitch / 90.0,
        1.0 if state.on_ground else -1.0,
        1.0 if state.in_water else -1.0,
        state.health / 20.0 - 1.0,
        state.food / 20.0 - 1.0,
        state.air / 300.0 - 1.0,
        state.light / 15.0 - 1.0,
        1.0 if state.is_day else -1.0,
        (state.biome.id if state.biome else -1) / 23.0,
        min(state.blocks.below, 255) / 128.0 - 1.0,
        min(state.blocks.head, 255) / 128.0 - 1.0,
        min(state.target.block, 255) / 128.0 - 1.0,
        min(state.target.distance, 8.0) / 4.0 - 1.0,
        1.0 if state.target.is_entity else -1.0,
        min(state.nearest_entity.distance if state.nearest_entity else 16.0, 16.0) / 8.0 - 1.0,
    ]


STATE_DIM = len(encode_state(State()))


class RNDCuriosity:
    """Random Network Distillation. Requer PyTorch."""

    def __init__(
        self,
        state_dim: int = STATE_DIM,
        hidden: int = 128,
        embed: int = 64,
        lr: float = 1e-4,
        scale: float = 1.0,
    ) -> None:
        try:
            import torch
            from torch import nn
        except ImportError as exc:  # pragma: no cover - depende do ambiente
            raise ImportError(
                "RNDCuriosity precisa de PyTorch. Instale com "
                "`pip install -r mods/CuriousMob/ai/requirements-train.txt` "
                "ou use CountBasedCuriosity."
            ) from exc

        self._torch = torch
        self.scale = scale

        def mlp() -> "nn.Module":
            return nn.Sequential(
                nn.Linear(state_dim, hidden),
                nn.ReLU(),
                nn.Linear(hidden, embed),
            )

        # A rede-alvo é aleatória e NUNCA treina — é o "oráculo" arbitrário
        # que o preditor tenta imitar. Congelamos os gradientes dela
        # explicitamente para que nenhum optimizer a atualize por engano.
        self.target = mlp()
        for param in self.target.parameters():
            param.requires_grad_(False)

        self.predictor = mlp()
        self.optimizer = torch.optim.Adam(self.predictor.parameters(), lr=lr)

        # Normalização online do erro: a escala bruta do erro de predição cai
        # ordens de grandeza durante o treino, e uma recompensa cuja escala
        # muda desestabiliza o PPO. Guardamos média/variância correntes.
        self._mean = 0.0
        self._var = 1.0
        self._count = 1e-4

    def reward(self, state: State) -> float:
        torch = self._torch
        features = torch.tensor([encode_state(state)], dtype=torch.float32)

        with torch.no_grad():
            target = self.target(features)
        predicted = self.predictor(features)
        loss = ((predicted - target) ** 2).mean()

        self.optimizer.zero_grad()
        loss.backward()
        self.optimizer.step()

        raw = float(loss.detach())
        self._update_stats(raw)
        return self.scale * raw / math.sqrt(self._var)

    def _update_stats(self, value: float) -> None:
        # Welford online: sem histórico acumulado, uso de RAM constante.
        self._count += 1
        delta = value - self._mean
        self._mean += delta / self._count
        self._var += (delta * (value - self._mean) - self._var) / self._count
        self._var = max(self._var, 1e-8)


class CombinedCuriosity:
    """Soma ponderada de várias fontes de curiosidade."""

    def __init__(self, sources: list[tuple[Curiosity, float]]) -> None:
        self.sources = sources

    def reward(self, state: State) -> float:
        return sum(weight * source.reward(state) for source, weight in self.sources)


def build_curiosity(memory: Memory, use_rnd: bool = False) -> Curiosity:
    """Fábrica com degradação graciosa: sem torch, cai para contagem."""
    counting = CountBasedCuriosity(memory)
    if not use_rnd:
        return counting
    try:
        return CombinedCuriosity([(counting, 1.0), (RNDCuriosity(), 1.0)])
    except ImportError:
        return counting
