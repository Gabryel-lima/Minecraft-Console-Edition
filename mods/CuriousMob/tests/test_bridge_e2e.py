"""Ponta a ponta do lado Python contra uma ponte falsa.

Sobe um servidor TCP que fala o protocolo do `CuriousMobBridge` (uma linha
JSON de estado, lê uma linha JSON de ação) e roda o `environment.py` de
verdade — mesmo processo, mesma função `main()` — contra ele.

O que isto cobre e os testes unitários não: o laço completo (conectar, ler
stream, decidir, escrever, persistir memória), o tratamento de linha
malformada, e o encerramento limpo quando o "jogo" fecha a conexão. O que
NÃO cobre: o lado C++ em runtime dentro do mundo — para isso só o roteiro
manual do README.md.
"""

from __future__ import annotations

import json
import socket
import sys
import threading
from pathlib import Path

import environment
from protocol.messages import Action


class FakeBridge:
    """Servidor TCP mínimo com a mesma etiqueta do CuriousMobBridge.

    Assíncrono nas duas direções, igual ao bridge real: o jogo empurra
    estados no seu próprio ritmo e lê ações numa thread separada, sem nunca
    esperar uma resposta por estado. Um fake em lockstep (escreve estado,
    bloqueia esperando a ação) travaria de propósito no primeiro estado que
    o agente decide não responder — como uma linha malformada — e o teste
    estaria medindo o fake, não o agente.
    """

    def __init__(self, states: list[str]) -> None:
        self.states = states
        self.received: list[dict] = []
        self._server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.bind(("127.0.0.1", 0))
        self._server.listen(1)
        self.port = self._server.getsockname()[1]
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self.error: Exception | None = None

    def start(self) -> None:
        self._thread.start()

    def _serve(self) -> None:
        try:
            conn, _ = self._server.accept()
            with conn:
                reader = threading.Thread(
                    target=self._read_replies, args=(conn,), daemon=True
                )
                reader.start()
                for line in self.states:
                    conn.sendall((line + "\n").encode("utf-8"))
                # Fecha só a direção de escrita: o cliente vê EOF e encerra o
                # laço, mas as ações que ele já mandou continuam chegando.
                conn.shutdown(socket.SHUT_WR)
                reader.join(timeout=10.0)
        except Exception as exc:  # pragma: no cover - só em falha de teste
            self.error = exc
        finally:
            self._server.close()

    def _read_replies(self, conn: socket.socket) -> None:
        buf = conn.makefile("rb")
        for raw in buf:
            line = raw.decode("utf-8").strip()
            if line:
                self.received.append(json.loads(line))

    def join(self, timeout: float = 10.0) -> None:
        self._thread.join(timeout)
        assert not self._thread.is_alive(), "ponte falsa não terminou"
        if self.error is not None:
            raise self.error


def state_line(tick: int, **overrides) -> str:
    payload = {
        "tick": tick,
        "x": float(tick), "y": 64.0, "z": 0.0,
        "yaw": 0.0, "pitch": 0.0,
        "on_ground": True, "alive": True,
        "health": 20.0, "max_health": 20.0, "food": 20, "air": 300,
        "biome": {"id": 1, "name": "Plains"},
        "blocks": {"feet": 0, "head": 0, "below": 2},
        "target": {"type": "none"},
        "inventory": {"selected": 0, "items": [], "armor": []},
        "last_result": {"attack": False, "use": False, "break": False, "place": False},
    }
    payload.update(overrides)
    return json.dumps(payload)


def run_client(monkeypatch, bridge: FakeBridge, memory_path: Path, extra: list[str] = []):
    argv = [
        "environment.py",
        "--port", str(bridge.port),
        "--memory", str(memory_path),
        *extra,
    ]
    monkeypatch.setattr(sys, "argv", argv)
    return environment.main()


def test_full_round_trip(monkeypatch, tmp_path):
    bridge = FakeBridge([state_line(t) for t in range(1, 21)])
    bridge.start()

    assert run_client(monkeypatch, bridge, tmp_path / "memory.json") == 0
    bridge.join()

    assert len(bridge.received) == 20
    for payload in bridge.received:
        # Toda ação enviada tem que ser uma Action válida e completa.
        action = Action.from_json(json.dumps(payload))
        assert action.move in ("forward", "back", "left", "right", "none")
        assert None not in payload.values()


def test_memory_is_persisted(monkeypatch, tmp_path):
    memory_path = tmp_path / "memory.json"
    bridge = FakeBridge([state_line(t, x=float(t * 20)) for t in range(1, 11)])
    bridge.start()

    run_client(monkeypatch, bridge, memory_path)
    bridge.join()

    assert memory_path.exists()
    saved = json.loads(memory_path.read_text(encoding="utf-8"))
    assert saved["total_visits"] == 10
    assert "Plains" in saved["biomes_seen"]
    # x de 20 a 200 atravessa vários chunks: a memória tem que registrar isso.
    assert len(saved["chunks"]) > 1


def test_memory_is_reused_across_sessions(monkeypatch, tmp_path):
    """Sem memória persistida, todo restart faria o mundo parecer novo."""
    memory_path = tmp_path / "memory.json"

    for _ in range(2):
        bridge = FakeBridge([state_line(t, x=0.0) for t in range(1, 6)])
        bridge.start()
        run_client(monkeypatch, bridge, memory_path)
        bridge.join()

    saved = json.loads(memory_path.read_text(encoding="utf-8"))
    assert saved["total_visits"] == 10  # 5 + 5, não 5


def test_malformed_line_does_not_kill_the_agent(monkeypatch, tmp_path, capsys):
    """Um JSON truncado (jogo fechando no meio de um write) é ignorado."""
    lines = [state_line(1), "{isto nao e json", state_line(2)]
    bridge = FakeBridge(lines)
    bridge.start()

    assert run_client(monkeypatch, bridge, tmp_path / "memory.json") == 0
    bridge.join()

    # A linha ruim não gera resposta, as boas geram.
    assert len(bridge.received) == 2
    assert "estado ignorado" in capsys.readouterr().err


def test_refused_connection_reports_cleanly(monkeypatch, tmp_path, capsys):
    """Sem o jogo rodando, a mensagem tem que dizer o que fazer."""
    free = socket.socket()
    free.bind(("127.0.0.1", 0))
    port = free.getsockname()[1]
    free.close()

    monkeypatch.setattr(
        sys, "argv",
        ["environment.py", "--port", str(port), "--memory", str(tmp_path / "m.json")],
    )
    assert environment.main() == 1
    assert "CURIOUSMOB_SPAWN" in capsys.readouterr().err


def test_random_policy_flag_still_works(monkeypatch, tmp_path):
    """A política da Etapa 1 continua disponível para diagnosticar a ponte."""
    bridge = FakeBridge([state_line(t) for t in range(1, 6)])
    bridge.start()

    run_client(monkeypatch, bridge, tmp_path / "m.json", extra=["--random"])
    bridge.join()

    assert len(bridge.received) == 5


def test_agent_handles_hostile_world(monkeypatch, tmp_path):
    """Lava, afogamento, fome e morte não podem derrubar o cliente."""
    lines = [
        state_line(1, in_lava=True),
        state_line(2, in_water=True, air=20),
        state_line(3, food=0),
        state_line(4, health=0.0, alive=False),
        state_line(5, target={"type": "entity", "distance": 1.5}),
        state_line(6, container={"size": 27, "items": []}),
        state_line(7, target={"type": "tile", "block": 54, "distance": 2.0}),
    ]
    bridge = FakeBridge(lines)
    bridge.start()

    assert run_client(monkeypatch, bridge, tmp_path / "m.json") == 0
    bridge.join()

    assert len(bridge.received) == len(lines)
    assert bridge.received[0]["move"] == "back"          # foge da lava
    assert bridge.received[3] == json.loads(Action().to_json())  # morto: nada
    assert bridge.received[4]["attack"] is True          # ataca o que está perto
    assert bridge.received[5]["close_container"] is True  # fecha o baú
    assert bridge.received[6]["use"] is True             # abre o baú mirado
