"""Testes da memória espacial (Etapa 5) e da curiosidade (Etapas 2 e 4)."""

from __future__ import annotations

import math

import pytest

from curiosity import CountBasedCuriosity, STATE_DIM, encode_state
from memory import DANGER_THRESHOLD, RECENT_POSITIONS, Memory
from protocol.messages import Biome, Blocks, State, Target


def state_at(x: float, z: float, **kwargs) -> State:
    return State(x=x, z=z, y=64.0, **kwargs)


def test_visit_counts_per_chunk():
    memory = Memory()
    for _ in range(3):
        memory.visit(state_at(0.0, 0.0))
    memory.visit(state_at(100.0, 100.0))

    assert memory.visits((0, 0)) == 3
    assert memory.visits((6, 6)) == 1
    assert memory.visits((99, 99)) == 0
    assert memory.total_visits == 4


def test_recent_positions_is_bounded():
    """Requisito do README: uso de RAM constante numa sessão indefinida."""
    memory = Memory()
    for i in range(RECENT_POSITIONS * 3):
        memory.visit(state_at(float(i), 0.0))
    assert len(memory.recent_positions) == RECENT_POSITIONS


def test_damage_marks_chunk_as_dangerous():
    memory = Memory()
    memory.visit(state_at(0.0, 0.0, health=20.0))
    assert not memory.is_dangerous((0, 0))

    memory.visit(state_at(0.0, 0.0, health=20.0 - DANGER_THRESHOLD))
    assert memory.is_dangerous((0, 0))
    assert list(memory.dangerous_chunks()) == [(0, 0)]


def test_damage_is_attributed_not_healing():
    """Recuperar vida não pode virar dano negativo e 'limpar' um chunk."""
    memory = Memory()
    memory.visit(state_at(0.0, 0.0, health=20.0))
    memory.visit(state_at(0.0, 0.0, health=10.0))
    memory.visit(state_at(0.0, 0.0, health=20.0))
    assert memory.chunks[(0, 0)].damage_taken == pytest.approx(10.0)


def test_frontier_excludes_known_chunks():
    memory = Memory()
    memory.visit(state_at(0.0, 0.0))
    memory.visit(state_at(16.0, 0.0))

    frontier = set(memory.frontier_chunks())
    assert (0, 0) not in frontier
    assert (1, 0) not in frontier
    assert (-1, 0) in frontier
    assert (2, 0) in frontier


def test_is_looping_detects_standing_still():
    memory = Memory()
    for _ in range(100):
        memory.visit(state_at(0.0, 0.0))
    assert memory.is_looping()


def test_is_looping_false_when_travelling():
    memory = Memory()
    for i in range(100):
        memory.visit(state_at(float(i) * 5, 0.0))
    assert not memory.is_looping()


def test_is_looping_needs_a_full_window():
    """Sem histórico suficiente não se afirma que está preso."""
    memory = Memory()
    for _ in range(10):
        memory.visit(state_at(0.0, 0.0))
    assert not memory.is_looping(window=60)


def test_memory_round_trip(tmp_path):
    memory = Memory()
    memory.visit(
        state_at(
            0.0, 0.0,
            biome=Biome(id=4, name="Forest"),
            blocks=Blocks(below=2, head=0),
            health=20.0,
        )
    )
    memory.visit(state_at(0.0, 0.0, health=10.0))

    path = tmp_path / "memory.json"
    memory.save(path)
    restored = Memory.load(path)

    assert restored.visits((0, 0)) == 2
    assert restored.biomes_seen == {"Forest"}
    assert 2 in restored.block_ids_seen
    assert restored.chunks[(0, 0)].damage_taken == pytest.approx(10.0)
    assert restored.chunks[(0, 0)].blocks_seen[2] == 1


def test_memory_load_missing_file_is_empty(tmp_path):
    memory = Memory.load(tmp_path / "nao-existe.json")
    assert memory.chunks == {}


# --- curiosidade -------------------------------------------------------


def test_novelty_decays_with_visits():
    memory = Memory()
    curiosity = CountBasedCuriosity(memory)

    memory.visit(state_at(0.0, 0.0))
    first = curiosity.reward(state_at(0.0, 0.0))

    for _ in range(99):
        memory.visit(state_at(0.0, 0.0))
    later = curiosity.reward(state_at(0.0, 0.0))

    assert later < first
    assert later == pytest.approx(1.0 / math.sqrt(100))


def test_unvisited_chunk_gets_maximum_novelty():
    memory = Memory()
    curiosity = CountBasedCuriosity(memory)
    # Nunca visitado: max(1, 0) == 1 visita, recompensa cheia — e não uma
    # divisão por zero.
    assert curiosity.reward(state_at(999.0, 999.0)) == pytest.approx(1.0)


def test_new_biome_is_rewarded_once():
    memory = Memory()
    curiosity = CountBasedCuriosity(memory)
    forest = state_at(0.0, 0.0, biome=Biome(id=4, name="Forest"))

    first = curiosity.reward(forest)
    second = curiosity.reward(forest)
    assert first - second == pytest.approx(5.0)  # NEW_BIOME_BONUS


def test_new_block_type_is_rewarded_once():
    memory = Memory()
    curiosity = CountBasedCuriosity(memory)
    with_block = state_at(0.0, 0.0, blocks=Blocks(below=56))  # minério de diamante

    first = curiosity.reward(with_block)
    second = curiosity.reward(with_block)
    assert first > second


def test_curiosity_does_not_double_count_visits():
    """`reward()` não pode chamar memory.visit() — quem visita é o laço."""
    memory = Memory()
    curiosity = CountBasedCuriosity(memory)
    memory.visit(state_at(0.0, 0.0))
    curiosity.reward(state_at(0.0, 0.0))
    assert memory.visits((0, 0)) == 1


# --- codificação de estado ---------------------------------------------


def test_encode_state_dimension_is_stable():
    assert len(encode_state(State())) == STATE_DIM


def test_encode_state_is_bounded():
    """Valores extremos não podem explodir fora do Box do observation_space."""
    extreme = State(
        x=1e6, y=255.0, z=-1e6, yaw=720.0, pitch=90.0,
        health=0.0, food=0, air=0, light=15.0,
        biome=Biome(id=22, name="Jungle"),
        blocks=Blocks(below=255, head=255),
        target=Target(type="tile", block=255, distance=1e3),
    )
    assert all(math.isfinite(v) and abs(v) <= 10.0 for v in encode_state(extreme))


def test_encode_state_handles_missing_optional_fields():
    """Estado sem bioma/entidade (mundo ainda carregando) não pode explodir."""
    assert all(math.isfinite(v) for v in encode_state(State()))
