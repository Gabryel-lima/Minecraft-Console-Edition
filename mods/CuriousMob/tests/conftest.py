"""Configuração comum dos testes do CuriousMob.

Os módulos de `ai/` se importam entre si por nome curto (`from memory import
Memory`), como se `ai/` fosse a raiz — é o que permite rodá-los direto
(`python ai/environment.py`) sem instalar um pacote. Para o pytest enxergar
o mesmo layout, ambos os diretórios entram no sys.path aqui.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "ai"))
