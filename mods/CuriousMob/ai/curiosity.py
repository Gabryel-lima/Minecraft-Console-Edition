"""Stub: sistema de curiosidade.

Etapa futura (ver mods/CuriousMob/PLANO.md, Etapa 4): Random Network
Distillation (RND) — recompensa o agente por estados difíceis de prever,
como aproximação de curiosidade intrínseca. Antes disso (Etapa 2), uma
versão mais simples baseada em contagem de visitas por região/chunk.
"""

from __future__ import annotations

from protocol.messages import State


def novelty_reward(state: State) -> float:
    """Recompensa de novidade para o estado atual. Hoje: não implementado."""
    raise NotImplementedError("curiosity.py é um stub — ver Etapas 2 e 4 do PLANO.md")
