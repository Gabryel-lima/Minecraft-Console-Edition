"""Round-trip real entre o JSON escrito/lido em C++ e o `json` do Python.

Compila `tests/cpp/json_harness.cpp` junto de `CuriousMobJson.cpp` (nenhuma
dependência do motor) e verifica os dois sentidos do protocolo:

- o que o C++ ESCREVE tem que ser consumível por `json.loads` e fiel aos
  valores originais;
- o que o Python ESCREVE (`Action.to_json`) tem que ser lido pelo C++ com os
  mesmos valores.

É o único teste que pega erros do serializador escrito à mão — vírgula
faltando num array vazio, escape de aspas, NaN, ou o campo `forward` sendo
confundido com o valor `"forward"` do campo `move`.

Pulado quando não há compilador C++ disponível.
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from protocol.messages import Action

ROOT = Path(__file__).resolve().parents[3]
CPP_DIR = ROOT / "4jcraft" / "Minecraft.Client" / "Mods" / "CuriousMob"
HARNESS = Path(__file__).resolve().parent / "cpp" / "json_harness.cpp"

COMPILER = shutil.which("g++") or shutil.which("clang++")

pytestmark = pytest.mark.skipif(
    COMPILER is None, reason="nenhum compilador C++ (g++/clang++) disponível"
)


@pytest.fixture(scope="module")
def harness(tmp_path_factory) -> Path:
    binary = tmp_path_factory.mktemp("cpp") / "json_harness"
    subprocess.run(
        [
            COMPILER, "-std=c++17", "-O0",
            "-o", str(binary),
            str(HARNESS), str(CPP_DIR / "CuriousMobJson.cpp"),
            "-I", str(CPP_DIR),
        ],
        check=True,
        capture_output=True,
    )
    return binary


def run_harness(binary: Path, mode: str, stdin: str = "") -> str:
    result = subprocess.run(
        [str(binary), mode], input=stdin, capture_output=True, text=True, check=True
    )
    return result.stdout


# --- C++ escreve, Python lê ---------------------------------------------


@pytest.fixture(scope="module")
def written_state(harness) -> dict:
    return json.loads(run_harness(harness, "write"))


def test_cpp_output_is_valid_json(written_state):
    assert written_state["tick"] == 500
    assert written_state["x"] == 12.5
    assert written_state["y"] == -64.25
    assert written_state["on_ground"] is True
    assert written_state["alive"] is False


def test_cpp_guards_nan_and_infinity(written_state):
    """NaN/inf não são JSON válido e derrubariam o agente no meio da sessão."""
    assert written_state["nan_guard"] == 0.0
    assert written_state["inf_guard"] == 0.0


def test_cpp_writes_nested_objects_and_arrays(written_state):
    assert written_state["biome"]["id"] == 4
    assert written_state["target"] == {"type": "tile", "face": 1}
    assert written_state["container"] is None
    assert written_state["nearest_entity"] is None
    assert written_state["inventory"]["selected"] == 2
    assert len(written_state["inventory"]["items"]) == 3
    assert written_state["inventory"]["items"][0]["id"] == 264
    # Array vazio: o caso em que uma vírgula sobrando quebraria o parse.
    assert written_state["inventory"]["armor"] == []


def test_cpp_converts_wstring_to_utf8(written_state):
    assert written_state["biome"]["name"] == "Floresta de Ação"


def test_cpp_escapes_strings(written_state):
    assert written_state["quoted"] == 'aspas:" barra:\\ nova-linha:\n tab:\t'


# --- Python escreve, C++ lê ---------------------------------------------


def parse_harness_output(text: str) -> dict[str, str]:
    return dict(line.split("=", 1) for line in text.strip().splitlines())


def test_cpp_reads_a_full_action(harness):
    action = Action(
        move="back", forward=-0.5, strafe=0.25, jump=True, sneak=True, sprint=False,
        turn=-12.5, look_pitch=7.25,
        attack=True, use=True, break_block=True, place_block=True,
        select_slot=3, swap_from=0, swap_to=8,
        drop=True, drop_stack=True, drop_all=True,
        release_item=True, stop_item=True, close_container=True,
    )
    parsed = parse_harness_output(run_harness(harness, "read", action.to_json()))

    assert parsed["move"] == "back"
    assert float(parsed["forward"]) == pytest.approx(-0.5)
    assert float(parsed["strafe"]) == pytest.approx(0.25)
    assert float(parsed["turn_num"]) == pytest.approx(-12.5)
    assert float(parsed["look_pitch"]) == pytest.approx(7.25)
    for flag in (
        "jump", "sneak", "attack", "use", "break_block", "place_block",
        "drop", "drop_stack", "drop_all", "release_item", "stop_item",
        "close_container",
    ):
        assert parsed[flag] == "1", f"C++ não leu a flag {flag}"
    assert parsed["sprint"] == "0"
    assert parsed["select_slot"] == "3"
    assert parsed["swap_from"] == "0"
    assert parsed["swap_to"] == "8"


def test_cpp_does_not_confuse_key_with_string_value(harness):
    """`{"move": "forward"}` não pode ser lido como o campo `forward`.

    Regressão: a busca de campo casava a ocorrência de `"forward"` como
    VALOR de `move` e lia o número do campo seguinte, fazendo o bot andar
    com um `forward` que ninguém mandou.
    """
    action = Action(move="forward", turn=-12.5)
    parsed = parse_harness_output(run_harness(harness, "read", action.to_json()))

    assert parsed["move"] == "forward"
    assert parsed["has_forward"] == "0"
    assert float(parsed["forward"]) == pytest.approx(-99.0)  # o fallback
    assert float(parsed["turn_num"]) == pytest.approx(-12.5)


def test_cpp_distinguishes_absent_from_present(harness):
    """`hasKey` é o que separa 'não mexe no slot' de 'seleciona o slot'."""
    parsed = parse_harness_output(run_harness(harness, "read", Action().to_json()))
    assert parsed["has_select_slot"] == "0"
    assert parsed["select_slot"] == "-1"  # fallback, não um slot real
    assert parsed["has_inexistente"] == "0"

    parsed = parse_harness_output(
        run_harness(harness, "read", Action(select_slot=0).to_json())
    )
    assert parsed["has_select_slot"] == "1"
    assert parsed["select_slot"] == "0"


def test_cpp_reads_categorical_turn(harness):
    """`turn` aceita string ("left") e número — os dois formatos convivem."""
    parsed = parse_harness_output(
        run_harness(harness, "read", Action(turn="left").to_json())
    )
    assert parsed["turn_str"] == "left"
    assert float(parsed["turn_num"]) == pytest.approx(0.0)


def test_cpp_accepts_numeric_booleans(harness):
    """Política discreta pode serializar flags como 0/1 em vez de true/false."""
    parsed = parse_harness_output(
        run_harness(harness, "read", '{"attack": 1, "use": 0, "jump": 1}')
    )
    assert parsed["attack"] == "1"
    assert parsed["use"] == "0"
    assert parsed["jump"] == "1"
