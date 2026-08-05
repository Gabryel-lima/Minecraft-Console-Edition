"""Stub: política do agente.

Etapa futura (ver mods/CuriousMob/PLANO.md, Etapa 3): treinar PPO
(stable-baselines3) sobre um ambiente Gymnasium que envolva o protocolo
definido em protocol/messages.py, substituindo a política aleatória usada
hoje em environment.py.
"""

from __future__ import annotations

from protocol.messages import Action, State


def decide(state: State) -> Action:
    """Decide a próxima ação dado o estado atual. Hoje: não implementado."""
    raise NotImplementedError("policy.py é um stub — política real vem na Etapa 3")
