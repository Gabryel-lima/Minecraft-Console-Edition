"""Memória espacial do agente — Etapa 5 do PLANO.md.

Registra o que o agente já viu para que ele possa reconhecer locais
favoritos, áreas perigosas e caminhos conhecidos. É a base de dados que a
curiosidade por contagem (curiosity.CountBasedCuriosity) consulta.

Restrição de projeto (ver README.md, "geradores, não listas"): o jogo roda
indefinidamente, então NADA aqui pode crescer sem limite. Concretamente:

- as contagens são por chunk (16x16), não por posição — o espaço de chaves é
  o número de chunks visitados, não o de ticks;
- `recent_positions` é um deque com maxlen, não uma lista;
- as consultas (`visited_chunks`, `dangerous_chunks`, ...) devolvem geradores,
  nunca listas materializadas.
"""

from __future__ import annotations

import json
import sys
from collections import Counter, deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from protocol.messages import State  # noqa: E402

Chunk = tuple[int, int]

# Quantas posições recentes guardar para detectar "andando em círculos".
RECENT_POSITIONS = 200

# Dano acumulado num chunk a partir do qual ele passa a ser "perigoso".
DANGER_THRESHOLD = 4.0


@dataclass
class ChunkMemory:
    """O que o agente sabe sobre um chunk."""

    visits: int = 0
    first_seen_tick: int = 0
    last_seen_tick: int = 0
    damage_taken: float = 0.0
    biome_id: int = -1
    biome_name: str = ""
    blocks_seen: Counter = field(default_factory=Counter)

    @property
    def is_dangerous(self) -> bool:
        return self.damage_taken >= DANGER_THRESHOLD


class Memory:
    """Memória de longo prazo do agente, indexada por chunk."""

    def __init__(self, danger_threshold: float = DANGER_THRESHOLD) -> None:
        self.danger_threshold = danger_threshold
        self.chunks: dict[Chunk, ChunkMemory] = {}
        self.recent_positions: deque[tuple[float, float, float]] = deque(
            maxlen=RECENT_POSITIONS
        )
        self.biomes_seen: set[str] = set()
        # Ids de bloco já vistos alguma vez — usado por "novidade de bloco".
        self.block_ids_seen: set[int] = set()
        self.total_visits = 0
        self._last_health: Optional[float] = None

    # -- escrita --

    def visit(self, state: State) -> ChunkMemory:
        """Registra um estado observado. Devolve a memória do chunk atual."""
        chunk = state.chunk()
        entry = self.chunks.get(chunk)
        if entry is None:
            entry = ChunkMemory(first_seen_tick=state.tick)
            self.chunks[chunk] = entry

        entry.visits += 1
        entry.last_seen_tick = state.tick
        self.total_visits += 1

        if state.biome is not None:
            entry.biome_id = state.biome.id
            entry.biome_name = state.biome.name
            if state.biome.name:
                self.biomes_seen.add(state.biome.name)

        for block_id in self.observed_blocks(state):
            entry.blocks_seen[block_id] += 1
            self.block_ids_seen.add(block_id)

        # Dano desde o último estado é atribuído ao chunk onde ocorreu — é
        # assim que "área perigosa" emerge sem ninguém rotular nada.
        if self._last_health is not None and state.health < self._last_health:
            entry.damage_taken += self._last_health - state.health
        self._last_health = state.health

        self.recent_positions.append(state.pos)
        return entry

    @staticmethod
    def observed_blocks(state: State) -> Iterator[int]:
        """Ids de bloco visíveis neste estado, ignorando ar e indisponíveis."""
        candidates = (
            state.blocks.feet,
            state.blocks.head,
            state.blocks.below,
            state.blocks.above,
            state.blocks.north,
            state.blocks.south,
            state.blocks.west,
            state.blocks.east,
        )
        for block_id in candidates:
            if block_id > 0:
                yield block_id
        if state.target.is_tile and state.target.block > 0:
            yield state.target.block

    # -- leitura (tudo gerador) --

    def visits(self, chunk: Chunk) -> int:
        entry = self.chunks.get(chunk)
        return entry.visits if entry is not None else 0

    def is_known(self, chunk: Chunk) -> bool:
        return chunk in self.chunks

    def is_dangerous(self, chunk: Chunk) -> bool:
        entry = self.chunks.get(chunk)
        return entry is not None and entry.damage_taken >= self.danger_threshold

    def visited_chunks(self) -> Iterator[tuple[Chunk, ChunkMemory]]:
        return iter(self.chunks.items())

    def dangerous_chunks(self) -> Iterator[Chunk]:
        return (c for c, m in self.chunks.items() if m.damage_taken >= self.danger_threshold)

    def favourite_chunks(self, top: int = 5) -> Iterator[Chunk]:
        """Chunks mais visitados que não sejam perigosos."""
        safe = (
            (c, m) for c, m in self.chunks.items()
            if m.damage_taken < self.danger_threshold
        )
        ranked = sorted(safe, key=lambda pair: pair[1].visits, reverse=True)
        return (c for c, _ in ranked[:top])

    def frontier_chunks(self) -> Iterator[Chunk]:
        """Chunks vizinhos de algum visitado, mas ainda não visitados.

        É a fronteira de exploração: para onde ir se o objetivo é ver algo
        novo sem se teletransportar para o desconhecido absoluto.
        """
        seen: set[Chunk] = set()
        for (cx, cz) in self.chunks:
            for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                neighbour = (cx + dx, cz + dz)
                if neighbour in self.chunks or neighbour in seen:
                    continue
                seen.add(neighbour)
                yield neighbour

    def is_looping(self, radius: float = 3.0, window: int = 60) -> bool:
        """True se as últimas `window` posições couberem num raio pequeno.

        Sinal de "andando em círculos"/preso, que a política usa para forçar
        uma virada. Não olha o histórico inteiro — só a janela recente.
        """
        if len(self.recent_positions) < window:
            return False
        recent = list(self.recent_positions)[-window:]
        cx = sum(p[0] for p in recent) / window
        cz = sum(p[2] for p in recent) / window
        return all(
            (p[0] - cx) ** 2 + (p[2] - cz) ** 2 <= radius * radius for p in recent
        )

    # -- persistência --

    def save(self, path: Path) -> None:
        """Grava a memória em JSON. Só chunks — o deque é volátil por design."""
        payload = {
            "total_visits": self.total_visits,
            "biomes_seen": sorted(self.biomes_seen),
            "block_ids_seen": sorted(self.block_ids_seen),
            "chunks": [
                {
                    "cx": cx,
                    "cz": cz,
                    "visits": m.visits,
                    "first_seen_tick": m.first_seen_tick,
                    "last_seen_tick": m.last_seen_tick,
                    "damage_taken": m.damage_taken,
                    "biome_id": m.biome_id,
                    "biome_name": m.biome_name,
                    "blocks_seen": dict(m.blocks_seen),
                }
                for (cx, cz), m in self.chunks.items()
            ],
        }
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload), encoding="utf-8")

    @staticmethod
    def load(path: Path) -> "Memory":
        memory = Memory()
        if not path.exists():
            return memory
        payload = json.loads(path.read_text(encoding="utf-8"))
        memory.total_visits = payload.get("total_visits", 0)
        memory.biomes_seen = set(payload.get("biomes_seen", []))
        memory.block_ids_seen = set(payload.get("block_ids_seen", []))
        for entry in payload.get("chunks", []):
            memory.chunks[(entry["cx"], entry["cz"])] = ChunkMemory(
                visits=entry.get("visits", 0),
                first_seen_tick=entry.get("first_seen_tick", 0),
                last_seen_tick=entry.get("last_seen_tick", 0),
                damage_taken=entry.get("damage_taken", 0.0),
                biome_id=entry.get("biome_id", -1),
                biome_name=entry.get("biome_name", ""),
                blocks_seen=Counter(
                    {int(k): v for k, v in entry.get("blocks_seen", {}).items()}
                ),
            )
        return memory
