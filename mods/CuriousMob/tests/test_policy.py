"""Testes da política (prioridade das regras e cobertura de mecânicas)."""

from __future__ import annotations

import json
import random

import pytest

from env import ACTIONS
from memory import Memory
from policy import TILE_CHEST, TILE_LAVA, TILE_STONE, CuriousPolicy
from protocol.messages import Action, Blocks, NearestEntity, State, Target


@pytest.fixture
def policy() -> CuriousPolicy:
    # Semente fixa: a política usa aleatoriedade para escolher rumo, e um
    # teste que dependesse dela seria intermitente.
    return CuriousPolicy(Memory(), rng=random.Random(1234))


def test_dead_agent_does_nothing(policy):
    assert policy.decide(State(alive=False)) == Action()


def test_lava_beats_everything(policy):
    """Fugir da lava tem prioridade sobre qualquer outra regra."""
    state = State(
        in_lava=True,
        food=0,                                   # também com fome
        target=Target(type="tile", block=TILE_CHEST, distance=1.0),  # e com um baú na mira
    )
    action = policy.decide(state)
    assert action.move == "back"
    assert not action.use


def test_attacks_entity_under_crosshair(policy):
    state = State(target=Target(type="entity", distance=2.0))
    action = policy.decide(state)
    assert action.attack is True


def test_turns_towards_close_entity_not_aimed_at(policy):
    state = State(
        yaw=0.0, nearest_entity=NearestEntity(dx=5.0, dz=0.0, distance=1.5)
    )
    action = policy.decide(state)
    assert isinstance(action.turn, float)
    assert action.turn != 0.0
    # Entidade em +x com yaw 0 (olhando +z) exige virar no sentido negativo.
    assert action.turn < 0


def test_turn_is_rate_limited(policy):
    """Uma virada de 180° num tick não é imitável por nenhuma política."""
    state = State(yaw=0.0, nearest_entity=NearestEntity(dx=0.0, dz=-5.0, distance=1.0))
    action = policy.decide(state)
    assert -15.0 <= float(action.turn) <= 15.0


def test_opens_interactive_block(policy):
    state = State(target=Target(type="tile", block=TILE_CHEST, distance=2.0))
    assert policy.decide(state).use is True


def test_closes_open_container(policy):
    from protocol.messages import ContainerState

    state = State(
        container=ContainerState(size=27),
        target=Target(type="tile", block=TILE_CHEST, distance=2.0),
    )
    action = policy.decide(state)
    assert action.close_container is True
    # Fechar vem ANTES de reabrir, senão o agente entra em laço abre/fecha.
    assert not action.use


def test_does_not_mine_liquids(policy):
    state = State(target=Target(type="tile", block=TILE_LAVA, distance=2.0))
    assert policy.decide(state).attack is False


def test_mines_novel_block(policy):
    state = State(target=Target(type="tile", block=TILE_STONE, distance=2.0))
    assert policy.decide(state).attack is True


def test_stops_mining_routine_block():
    """Bloco já visto muitas vezes neste chunk deixa de ser interessante."""
    memory = Memory()
    # rng determinístico que nunca dispara o 2% de re-mineração aleatória.
    policy = CuriousPolicy(memory, rng=random.Random(0))
    state = State(target=Target(type="tile", block=TILE_STONE, distance=2.0),
                  blocks=Blocks(below=TILE_STONE))
    for _ in range(50):
        memory.visit(state)

    attacks = sum(1 for _ in range(100) if policy.decide(state).attack)
    assert attacks < 20  # cai para a taxa residual, não 100%


def test_out_of_reach_target_is_ignored(policy):
    state = State(target=Target(type="tile", block=TILE_CHEST, distance=9.0))
    action = policy.decide(state)
    assert not action.use
    assert not action.attack


def test_explores_when_nothing_else_applies(policy):
    action = policy.decide(State())
    assert action.move == "forward"


def test_never_emits_an_unserialisable_action(policy):
    """Qualquer ação produzida tem que virar JSON válido para a ponte."""
    rng = random.Random(7)
    for _ in range(300):
        state = State(
            x=rng.uniform(-500, 500), z=rng.uniform(-500, 500),
            y=rng.uniform(0, 128), yaw=rng.uniform(-360, 360),
            health=rng.uniform(0, 20), food=rng.randint(0, 20),
            air=rng.randint(0, 300),
            in_water=rng.random() < 0.2, in_lava=rng.random() < 0.1,
            alive=rng.random() < 0.95,
            blocks=Blocks(feet=rng.randint(0, 100), head=rng.randint(0, 100)),
            target=Target(
                type=rng.choice(["none", "tile", "entity"]),
                block=rng.randint(0, 160), distance=rng.uniform(0, 8),
            ),
        )
        action = policy.decide(state)
        payload = json.loads(action.to_json())
        assert "move" in payload
        assert None not in payload.values()


def test_env_action_list_covers_the_main_mechanics():
    """O espaço de ação do PPO tem que exercitar mais que andar."""
    emitted = [factory() for factory in ACTIONS]
    assert any(a.attack for a in emitted)
    assert any(a.use for a in emitted)
    assert any(a.jump for a in emitted)
    assert any(a.sneak for a in emitted)
    assert any(a.sprint for a in emitted)
    assert any(a.drop for a in emitted)
    assert any(a.close_container for a in emitted)
    assert any(float(a.turn) < 0 for a in emitted if isinstance(a.turn, float))
    assert any(a.look_pitch != 0 for a in emitted)


def test_env_action_list_is_serialisable():
    for factory in ACTIONS:
        json.loads(factory().to_json())
