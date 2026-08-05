"""Paridade de mecânicas entre o Player e o BotPlayer.

O requisito do PLANO.md é que o agente seja "um jogador headless", não um mob
com um punhado de ações. Este arquivo é a verificação automatizada desse
requisito, em três camadas:

1. Toda mecânica listada em `PLAYER_MECHANICS` tem um campo correspondente na
   `Action` (ou é observável na `State`).
2. Esse campo é REALMENTE LIDO pelo C++ — testado por leitura do fonte de
   `CuriousMobController.cpp`. Sem isso o teste seria uma tautologia sobre o
   dataclass Python.
3. O `BotPlayer` roteia cada mecânica pela mesma primitiva do motor que o
   jogador real usa — testado por leitura de `BotPlayer.cpp`.

O que estes testes NÃO cobrem: que a mecânica funcione em runtime dentro do
mundo (isso exige o jogo rodando; ver o roteiro manual no README.md). Eles
cobrem que o caminho existe e está ligado ponta a ponta.
"""

from __future__ import annotations

from dataclasses import fields
from pathlib import Path

import pytest

from protocol.messages import Action, State

# tests/ -> CuriousMob/ -> mods/ -> raiz do repo
CPP_DIR = (
    Path(__file__).resolve().parents[3]
    / "4jcraft"
    / "Minecraft.Client"
    / "Mods"
    / "CuriousMob"
)


@pytest.fixture(scope="module")
def controller_source() -> str:
    return (CPP_DIR / "CuriousMobController.cpp").read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def bot_source() -> str:
    return (CPP_DIR / "BotPlayer.cpp").read_text(encoding="utf-8")


# Mecânica de jogador -> campo da Action que a dispara.
PLAYER_MECHANICS = {
    "andar para frente/trás": "move",
    "andar de lado (strafe)": "strafe",
    "movimento analógico": "forward",
    "pular": "jump",
    "agachar": "sneak",
    "correr": "sprint",
    "virar a cabeça (yaw)": "turn",
    "olhar para cima/baixo (pitch)": "look_pitch",
    "bater / minerar (botão esquerdo)": "attack",
    "usar bloco/entidade/item (botão direito)": "use",
    "quebrar bloco (atalho)": "break_block",
    "colocar bloco (atalho)": "place_block",
    "selecionar slot da hotbar": "select_slot",
    "mover item entre slots": "swap_from",
    "soltar item": "drop",
    "soltar pilha inteira": "drop_stack",
    "soltar inventário inteiro": "drop_all",
    "soltar item em uso (arco/comida)": "release_item",
    "cancelar item em uso": "stop_item",
    "fechar container": "close_container",
}

# Mecânica observável -> campo da State que a expõe.
PLAYER_OBSERVATIONS = {
    "posição": "x",
    "rotação": "yaw",
    "velocidade": "vx",
    "no chão": "on_ground",
    "na água": "in_water",
    "na lava": "in_lava",
    "agachado": "sneaking",
    "correndo": "sprinting",
    "dormindo": "sleeping",
    "usando item": "using_item",
    "vivo": "alive",
    "vida": "health",
    "fome": "food",
    "saturação": "saturation",
    "ar (afogamento)": "air",
    "experiência": "xp_level",
    "hora do dia": "day_time",
    "luz": "light",
    "bioma": "biome",
    "alvo sob a mira": "target",
    "blocos vizinhos": "blocks",
    "entidade mais próxima": "nearest_entity",
    "inventário": "inventory",
    "container aberto": "container",
    "resultado da última ação": "last_result",
}


@pytest.mark.parametrize(
    "mechanic,field_name", sorted(PLAYER_MECHANICS.items()), ids=lambda v: str(v)
)
def test_mechanic_has_action_field(mechanic: str, field_name: str):
    assert field_name in {f.name for f in fields(Action)}, (
        f"a mecânica '{mechanic}' não tem campo '{field_name}' na Action"
    )


@pytest.mark.parametrize(
    "observation,field_name", sorted(PLAYER_OBSERVATIONS.items()), ids=lambda v: str(v)
)
def test_observation_has_state_field(observation: str, field_name: str):
    assert field_name in {f.name for f in fields(State)}, (
        f"a observação '{observation}' não tem campo '{field_name}' na State"
    )


@pytest.mark.parametrize("field_name", sorted(set(PLAYER_MECHANICS.values())))
def test_action_field_is_read_by_cpp(field_name: str, controller_source: str):
    """O campo não basta existir no Python: o controller tem que lê-lo."""
    assert f'"{field_name}"' in controller_source, (
        f"CuriousMobController.cpp nunca lê o campo de ação '{field_name}' — "
        "o campo existe no protocolo mas não tem efeito no jogo"
    )
    # `swap_to` acompanha `swap_from` mas não está no dicionário de mecânicas
    # (uma mecânica, dois campos); confere aqui para não passar despercebido.
    if field_name == "swap_from":
        assert '"swap_to"' in controller_source


@pytest.mark.parametrize(
    "observation_field", sorted(set(PLAYER_OBSERVATIONS.values()))
)
def test_state_field_is_written_by_cpp(observation_field: str, controller_source: str):
    assert f'"{observation_field}"' in controller_source, (
        f"CuriousMobController.cpp nunca escreve o campo de estado "
        f"'{observation_field}'"
    )


# Cada clique do bot deve cair na MESMA primitiva do motor que o jogador real
# usa. Se alguém trocar `interact()` por uma reimplementação caseira, este
# teste quebra.
ENGINE_PRIMITIVES = {
    "atacar entidade": "attack(target.entity)",
    "interagir com entidade": "interact(target.entity)",
    "usar bloco": "Tile::tiles[t]->use(",
    "usar item sobre bloco": "held->useOn(",
    "usar item no ar": "held->use(level, self)",
    "desgaste da ferramenta ao minerar": "item->mineBlock(",
    "drops ao quebrar bloco": "playerDestroy(",
    "aviso de destruição ao bloco": "playerWillDestroy(",
    "soltar item": "drop(wholeStack)",
    "esvaziar inventário": "inventory->dropAll()",
    "fechar container": "closeContainer()",
}


@pytest.mark.parametrize(
    "mechanic,primitive", sorted(ENGINE_PRIMITIVES.items()), ids=lambda v: str(v)
)
def test_bot_uses_engine_primitive(mechanic: str, primitive: str, bot_source: str):
    assert primitive in bot_source, (
        f"BotPlayer.cpp não usa a primitiva do motor '{primitive}' para "
        f"'{mechanic}' — paridade com o Player quebrada"
    )


def test_bot_does_not_bypass_trust_system(bot_source: str):
    """Ações destrutivas passam pelas checagens de privilégio do Player.

    São as mesmas checagens que o jogo aplica a um jogador humano num mundo
    com o sistema de confiança ligado. Ignorá-las daria ao bot poderes que
    nenhum jogador tem.
    """
    for guard in (
        "isAllowedToMine()",
        "isAllowedToHurtEntity(",
        "isAllowedToInteract(",
        "isAllowedToUse(",
        "mayDestroyBlockAt(",
    ):
        assert guard in bot_source, f"BotPlayer.cpp não checa {guard}"


def test_bot_is_not_creative(bot_source: str):
    """O bot nasce em sobrevivência — senão fome/dano/durabilidade não valem."""
    assert "abilities.instabuild = false;" in bot_source
    assert "abilities.invulnerable = false;" in bot_source
    assert "abilities.flying = false;" in bot_source


def test_pick_matches_player_reach(bot_source: str):
    """O alcance do bot é o do jogador, não um raio arbitrário maior."""
    assert "REACH" in bot_source or "REACH" in (CPP_DIR / "BotPlayer.h").read_text(
        encoding="utf-8"
    )
    header = (CPP_DIR / "BotPlayer.h").read_text(encoding="utf-8")
    assert "REACH = 4.5" in header
