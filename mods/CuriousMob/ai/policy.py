"""Política do agente.

Duas implementações:

- `CuriousPolicy`: política escrita à mão, dirigida por curiosidade e
  sobrevivência. É a que roda por padrão em `environment.py`. Não é o
  objetivo final do PLANO.md (que é PPO), mas cumpre dois papéis
  indispensáveis: (a) dar um comportamento base coerente enquanto não há
  política treinada, e (b) servir de *verificação viva* de que TODA mecânica
  de jogador exposta pela ponte é de fato alcançável a partir do estado —
  cada `Action` que ela emite exercita um caminho diferente do BotPlayer.

- `TrainedPolicy`: carrega um modelo Stable-Baselines3 salvo por `train.py`
  e o consulta. Sem SB3/torch instalados, levanta ImportError.

A política por si só não guarda histórico: recebe `State` e `Memory` (que é
quem tem a memória) e devolve uma `Action`. Isso a torna trivialmente
testável e evita duplicar estado entre módulos.
"""

from __future__ import annotations

import math
import random
import sys
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from protocol.messages import Action, State  # noqa: E402

from memory import Memory  # noqa: E402

# Ids de tile do jogo usados por decisões específicas. Os ids são os
# clássicos do Minecraft e batem com Tile::*_Id no lado C++.
TILE_AIR = 0
TILE_STONE = 1
TILE_DIRT = 3
TILE_WATER = 8
TILE_FLOWING_WATER = 9
TILE_LAVA = 10
TILE_FLOWING_LAVA = 11
TILE_CHEST = 54
TILE_FURNACE = 61
TILE_LIT_FURNACE = 62
TILE_CRAFTING_TABLE = 58
TILE_WOODEN_DOOR = 64
TILE_LEVER = 69
TILE_STONE_BUTTON = 77
TILE_WOODEN_BUTTON = 143

# Blocos cujo clique direito abre uma interface ou aciona um mecanismo —
# alvos naturais para um agente "curioso" testar `use`.
INTERACTIVE_TILES = frozenset(
    {
        TILE_CHEST,
        TILE_FURNACE,
        TILE_LIT_FURNACE,
        TILE_CRAFTING_TABLE,
        TILE_WOODEN_DOOR,
        TILE_LEVER,
        TILE_STONE_BUTTON,
        TILE_WOODEN_BUTTON,
    }
)

LIQUIDS = frozenset({TILE_WATER, TILE_FLOWING_WATER, TILE_LAVA, TILE_FLOWING_LAVA})
DANGEROUS_LIQUIDS = frozenset({TILE_LAVA, TILE_FLOWING_LAVA})


class CuriousPolicy:
    """Sobrevivência primeiro, curiosidade depois.

    A ordem das regras é a prioridade: fugir de lava vence explorar, comer
    vence minerar. A última regra é sempre "explore", então a política nunca
    devolve uma ação vazia — um agente parado não gera dado nenhum.
    """

    def __init__(self, memory: Memory, rng: Optional[random.Random] = None) -> None:
        self.memory = memory
        self.rng = rng or random.Random()
        # Direção de exploração corrente, mantida por alguns ticks para o bot
        # andar em linha reta em vez de tremer no lugar trocando de rumo a
        # cada decisão.
        self._commit_ticks = 0
        self._commit_turn = 0.0

    def decide(self, state: State) -> Action:
        if not state.alive:
            # Morto: nada a fazer até o jogo respawnar o bot. Devolver ação
            # vazia evita gastar decisões (e ruído no aprendizado) num estado
            # em que nenhum input tem efeito.
            return Action()

        for rule in (
            self._escape_danger,
            self._eat_if_hungry,
            self._surface_if_drowning,
            self._defend,
            self._investigate_container,
            self._investigate_target,
            self._unstick,
        ):
            action = rule(state)
            if action is not None:
                return action

        return self._explore(state)

    # -- regras, em ordem de prioridade --

    def _escape_danger(self, state: State) -> Optional[Action]:
        """Lava sob os pés ou à frente: recuar e virar, sem pensar em mais nada."""
        if state.in_lava or state.blocks.feet in DANGEROUS_LIQUIDS:
            return Action(move="back", turn=25.0, jump=True, sprint=True)
        if state.blocks.north in DANGEROUS_LIQUIDS or state.blocks.south in DANGEROUS_LIQUIDS:
            return Action(move="back", turn=45.0)
        return None

    def _eat_if_hungry(self, state: State) -> Optional[Action]:
        """Come se houver comida e a fome estiver baixa.

        Não sabemos quais ids são comida sem uma tabela de itens, então a
        heurística é: com fome e com algo na mão, tenta `use` no ar (que é
        exatamente o caminho de comer do jogador). Se o item não for comida,
        `use` simplesmente não tem efeito e `last_result.use` volta False —
        custo de um tick, sem risco.
        """
        if not state.is_starving() or state.inventory is None:
            return None
        if state.inventory.held is None:
            # Nada na mão: tenta o próximo slot ocupado, se houver.
            for stack in state.inventory.hotbar():
                if stack.slot != state.inventory.selected:
                    return Action(select_slot=stack.slot)
            return None
        # Olha para cima para não mirar num bloco: `use` só cai no caminho
        # "usar item no ar" (comer/beber) quando não há nada sob a mira.
        return Action(use=True, look_pitch=-20.0 if state.pitch > -60 else 0.0)

    def _surface_if_drowning(self, state: State) -> Optional[Action]:
        """Sem ar debaixo d'água: subir."""
        if state.in_water and state.air < 150:
            return Action(move="forward", jump=True, look_pitch=-15.0)
        return None

    def _defend(self, state: State) -> Optional[Action]:
        """Ataca o que estiver na mira e perto o bastante para ser ameaça."""
        if state.target.is_entity and state.target.distance <= 3.0:
            return Action(attack=True, move="forward")
        entity = state.nearest_entity
        if entity is not None and entity.distance <= 2.0:
            # Perto mas fora da mira: gira na direção dele antes de bater.
            return Action(turn=self._turn_towards(state, entity.dx, entity.dz))
        return None

    def _investigate_container(self, state: State) -> Optional[Action]:
        """Se um container ficou aberto, olha o conteúdo e fecha.

        Deixar um baú aberto trava o agente num estado em que boa parte do
        input do jogador é ignorada, então fechar é obrigatório — não é uma
        preferência de comportamento.
        """
        if state.container is None:
            return None
        return Action(close_container=True)

    def _investigate_target(self, state: State) -> Optional[Action]:
        """Curiosidade aplicada ao que está sob a mira.

        Bloco interativo nunca visto -> `use` (abre baú, aciona alavanca,
        abre porta). Bloco comum e novo para a memória -> `attack` (minerar),
        que é como o agente coleta e descobre o que há embaixo.
        """
        target = state.target
        if not target.is_tile or target.distance > 4.5:
            return None

        if target.block in INTERACTIVE_TILES:
            return Action(use=True)

        # Minerar só o que ainda não é rotina neste chunk, e nunca líquido
        # (quebrar água/lava não faz nada útil e pode inundar o agente).
        if target.block <= 0 or target.block in LIQUIDS:
            return None
        entry = self.memory.chunks.get(state.chunk())
        seen_here = entry.blocks_seen.get(target.block, 0) if entry else 0
        if seen_here < 8 or self.rng.random() < 0.02:
            return Action(attack=True)
        return None

    def _unstick(self, state: State) -> Optional[Action]:
        """Andando em círculos ou preso numa parede: vira forte e pula."""
        blocked = state.blocks.head > 0 and state.blocks.head not in LIQUIDS
        if blocked:
            return Action(move="forward", jump=True, turn=self.rng.uniform(-30, 30))
        if self.memory.is_looping():
            self._commit_ticks = 0
            return Action(move="forward", turn=self.rng.uniform(90, 180), sprint=True)
        return None

    def _explore(self, state: State) -> Action:
        """Anda em frente, corrigindo o rumo na direção do chunk menos visitado."""
        if self._commit_ticks > 0:
            self._commit_ticks -= 1
            return Action(
                move="forward",
                turn=self._commit_turn,
                sprint=state.food > 6,
                jump=state.blocks.north > 0 or state.blocks.south > 0,
            )

        self._commit_ticks = self.rng.randint(20, 60)
        self._commit_turn = self._turn_towards_novelty(state)
        return Action(move="forward", turn=self._commit_turn, sprint=state.food > 6)

    # -- utilidades de navegação --

    @staticmethod
    def _turn_towards(state: State, dx: float, dz: float) -> float:
        """Delta de yaw (graus, limitado) que aponta o bot para (dx, dz).

        O yaw do Minecraft é 0 = +Z e cresce no sentido horário visto de
        cima, daí o atan2(-dx, dz).
        """
        desired = math.degrees(math.atan2(-dx, dz))
        delta = (desired - state.yaw + 180.0) % 360.0 - 180.0
        # Limita a 15°/tick: uma virada instantânea de 180° é fisicamente
        # possível para o bot mas produz um sinal de treino que nenhuma
        # política aprenderia a imitar a partir de observação.
        return max(-15.0, min(15.0, delta))

    def _turn_towards_novelty(self, state: State) -> float:
        """Escolhe o rumo em direção ao vizinho de chunk menos visitado."""
        cx, cz = state.chunk()
        best_delta = self.rng.uniform(-20.0, 20.0)
        best_visits: Optional[int] = None

        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            neighbour = (cx + dx, cz + dz)
            if self.memory.is_dangerous(neighbour):
                continue
            visits = self.memory.visits(neighbour)
            if best_visits is None or visits < best_visits:
                best_visits = visits
                best_delta = self._turn_towards(state, float(dx), float(dz))
        return best_delta


class TrainedPolicy:
    """Consulta um modelo PPO salvo por `train.py`. Requer stable-baselines3."""

    def __init__(self, model_path: Path) -> None:
        try:
            from stable_baselines3 import PPO
        except ImportError as exc:  # pragma: no cover - depende do ambiente
            raise ImportError(
                "TrainedPolicy precisa de stable-baselines3. Instale com "
                "`pip install -r mods/CuriousMob/ai/requirements-train.txt`."
            ) from exc

        from env import ACTIONS, encode_observation  # noqa: F401

        self._model = PPO.load(str(model_path))
        self._actions = ACTIONS
        self._encode = encode_observation

    def decide(self, state: State) -> Action:
        import numpy as np

        obs = np.array(self._encode(state), dtype=np.float32)
        index, _ = self._model.predict(obs, deterministic=False)
        return self._actions[int(index)]()


def decide(state: State, memory: Optional[Memory] = None) -> Action:
    """Atalho funcional para quem só quer uma ação sem montar objetos.

    Cria uma memória descartável a cada chamada, então NÃO use isto no laço
    principal: sem memória persistente não há curiosidade nem exploração
    dirigida. É conveniência para testes e para o REPL.
    """
    return CuriousPolicy(memory or Memory()).decide(state)
