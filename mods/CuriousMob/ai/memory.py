"""Stub: memória do agente.

Etapa futura (ver mods/CuriousMob/PLANO.md, Etapa 5): registrar chunks
visitados, blocos raros encontrados, biomas e entidades conhecidas, para o
agente reconhecer locais favoritos, áreas perigosas e caminhos conhecidos.
"""

from __future__ import annotations

from protocol.messages import State


class Memory:
    """Memória espacial do agente. Hoje: não implementado."""

    def __init__(self) -> None:
        raise NotImplementedError("memory.py é um stub — ver Etapa 5 do PLANO.md")

    def visit(self, state: State) -> None:
        raise NotImplementedError
