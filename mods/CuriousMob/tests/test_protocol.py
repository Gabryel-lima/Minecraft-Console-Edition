"""Testes do protocolo: parse do estado e serialização da ação."""

from __future__ import annotations

import json

from protocol.messages import Action, State

# Um estado completo, no formato que CuriousMobController::buildStateJson
# produz. Se este literal e o C++ divergirem, é aqui que o teste quebra.
FULL_STATE = json.dumps(
    {
        "tick": 500,
        "x": 12.5, "y": 64.0, "z": -3.25,
        "yaw": 90.0, "pitch": -10.0,
        "vx": 0.1, "vy": -0.08, "vz": 0.0,
        "on_ground": True, "in_water": False, "in_lava": False,
        "sneaking": False, "sprinting": True, "sleeping": False,
        "using_item": False, "alive": True,
        "health": 18.0, "max_health": 20.0, "air": 300,
        "xp_level": 3, "xp_progress": 0.4,
        "food": 15, "saturation": 2.5,
        "day_time": 1200, "is_day": True, "light": 15.0,
        "biome": {"id": 4, "name": "Forest"},
        "block_in_front": 3,
        "target": {
            "type": "tile", "x": 13, "y": 64, "z": -3, "face": 1,
            "block": 2, "data": 0, "distance": 1.8,
        },
        "blocks": {
            "feet": 0, "head": 0, "below": 2, "above": 0,
            "north": 0, "south": 3, "west": 0, "east": 1,
        },
        "nearest_entity": {
            "type": 7, "name": "Cow", "dx": 3.0, "dy": 0.0, "dz": 4.0,
            "distance": 5.0, "health": 10.0,
        },
        "inventory": {
            "selected": 2,
            "items": [{"slot": 2, "id": 278, "count": 1, "aux": 5}],
            "armor": [{"slot": 3, "id": 306, "count": 1, "aux": 0}],
            "held": {"slot": 2, "id": 278, "count": 1, "aux": 5},
        },
        "container": {
            "size": 27,
            "items": [{"slot": 0, "id": 264, "count": 3, "aux": 0}],
        },
        "last_result": {"attack": True, "use": False, "break": True, "place": False},
    }
)


def test_parses_full_state():
    state = State.from_json(FULL_STATE)

    assert state.tick == 500
    assert state.pos == (12.5, 64.0, -3.25)
    assert state.sprinting is True
    assert state.biome is not None and state.biome.name == "Forest"

    assert state.target.is_tile
    assert state.target.block == 2
    assert state.target.face == 1

    assert state.blocks.below == 2
    assert state.nearest_entity is not None
    assert state.nearest_entity.name == "Cow"

    assert state.inventory is not None
    assert state.inventory.selected == 2
    assert state.inventory.held is not None and state.inventory.held.id == 278
    assert state.inventory.find_slot(278) == 2

    assert state.container is not None and state.container.size == 27

    # "break" no fio vira `break_` no dataclass (palavra reservada).
    assert state.last_result.break_ is True
    assert state.last_result.place is False


def test_state_parsing_is_tolerant():
    """Estado antigo (Etapa 1) e campos desconhecidos não devem derrubar nada."""
    legacy = json.dumps(
        {
            "tick": 1, "x": 0.0, "y": 64.0, "z": 0.0, "yaw": 0.0, "pitch": 0.0,
            "on_ground": True, "health": 20.0, "food": 20, "block_in_front": 1,
        }
    )
    state = State.from_json(legacy)
    assert state.block_in_front == 1
    assert state.target.type == "none"      # default, não erro
    assert state.inventory is None
    assert state.biome is None

    with_unknown = json.dumps({"tick": 2, "campo_do_futuro": 42})
    assert State.from_json(with_unknown).tick == 2


def test_chunk_uses_negative_floor_division():
    """x=-1 pertence ao chunk -1, não ao 0 — erro clássico de >> vs /."""
    assert State(x=0.0, z=0.0).chunk() == (0, 0)
    assert State(x=15.0, z=15.0).chunk() == (0, 0)
    assert State(x=16.0, z=16.0).chunk() == (1, 1)
    assert State(x=-1.0, z=-1.0).chunk() == (-1, -1)
    assert State(x=-16.0, z=-16.0).chunk() == (-1, -1)
    assert State(x=-17.0, z=-17.0).chunk() == (-2, -2)


def test_action_omits_none_fields():
    """None significa 'não mexe'; mandar null reescreveria o slot no C++."""
    payload = json.loads(Action(move="forward").to_json())
    assert payload["move"] == "forward"
    assert "select_slot" not in payload
    assert "swap_from" not in payload

    payload = json.loads(Action(select_slot=0).to_json())
    assert payload["select_slot"] == 0


def test_action_round_trip():
    original = Action(
        move="back", jump=True, sneak=True, turn=12.5, look_pitch=-5.0,
        attack=True, use=True, select_slot=4, drop_stack=True,
    )
    assert Action.from_json(original.to_json()) == original
