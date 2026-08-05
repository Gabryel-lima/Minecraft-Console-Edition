"""(De)serialização das mensagens do protocolo CuriousMob.

Espelha mods/CuriousMob/protocol/messages.md. Uma mensagem por linha, JSON,
trafegada em um socket TCP newline-delimited.

Compatibilidade: `State.from_json` é TOLERANTE — campos ausentes caem no
default e campos desconhecidos são ignorados. Isso é deliberado: o jogo e o
processo Python são compilados/rodados separadamente, então é normal que um
lado esteja uma versão à frente do outro durante o desenvolvimento, e um
`TypeError: unexpected keyword argument` derrubaria o agente no meio de uma
sessão longa de treino por causa de um campo novo.
"""

from __future__ import annotations

import json
import math
from dataclasses import asdict, dataclass, field, fields
from typing import Any, Iterator, Optional

# --- Sub-estruturas do estado ------------------------------------------


@dataclass
class ItemStack:
    """Uma pilha de itens num slot. `slot` é o índice dentro do container."""

    slot: int = -1
    id: int = 0
    count: int = 0
    aux: int = 0


@dataclass
class Target:
    """O que está sob a mira do bot — o análogo do `hitResult` do jogador.

    `type` é "tile", "entity" ou "none". Os campos de bloco só têm sentido
    para "tile"; os de entidade, só para "entity".
    """

    type: str = "none"
    x: int = 0
    y: int = 0
    z: int = 0
    face: int = 0
    block: int = 0
    data: int = 0
    distance: float = 0.0
    entity_type: int = 0
    entity_name: str = ""
    entity_health: float = 0.0

    @property
    def is_tile(self) -> bool:
        return self.type == "tile"

    @property
    def is_entity(self) -> bool:
        return self.type == "entity"


@dataclass
class Blocks:
    """Vizinhança imediata da célula do bot (ids de tile, -1 = indisponível)."""

    feet: int = 0
    head: int = 0
    below: int = 0
    above: int = 0
    north: int = 0
    south: int = 0
    west: int = 0
    east: int = 0


@dataclass
class NearestEntity:
    type: int = 0
    name: str = ""
    dx: float = 0.0
    dy: float = 0.0
    dz: float = 0.0
    distance: float = 0.0
    health: float = 0.0


@dataclass
class Biome:
    id: int = -1
    name: str = ""


@dataclass
class InventoryState:
    selected: int = 0
    items: list[ItemStack] = field(default_factory=list)
    armor: list[ItemStack] = field(default_factory=list)
    held: Optional[ItemStack] = None

    def hotbar(self) -> Iterator[ItemStack]:
        """Só os 9 slots da hotbar, que são os selecionáveis por `select_slot`."""
        return (stack for stack in self.items if 0 <= stack.slot < 9)

    def find_slot(self, item_id: int) -> int:
        """Primeiro slot que contém `item_id`, ou -1."""
        for stack in self.items:
            if stack.id == item_id and stack.count > 0:
                return stack.slot
        return -1


@dataclass
class ContainerState:
    """Container aberto (baú, fornalha, bancada...). None quando fechado."""

    size: int = 0
    items: list[ItemStack] = field(default_factory=list)


@dataclass
class LastResult:
    """Se a ação do tick anterior teve efeito. Sinal de recompensa direto."""

    attack: bool = False
    use: bool = False
    break_: bool = False
    place: bool = False


# --- Estado -------------------------------------------------------------


@dataclass
class State:
    tick: int = 0

    # Corpo
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    yaw: float = 0.0
    pitch: float = 0.0
    vx: float = 0.0
    vy: float = 0.0
    vz: float = 0.0

    # Situação
    on_ground: bool = True
    in_water: bool = False
    in_lava: bool = False
    sneaking: bool = False
    sprinting: bool = False
    sleeping: bool = False
    using_item: bool = False
    alive: bool = True

    # Vitais
    health: float = 20.0
    max_health: float = 20.0
    food: int = 20
    saturation: float = 0.0
    air: int = 300
    xp_level: int = 0
    xp_progress: float = 0.0

    # Mundo
    day_time: int = 0
    is_day: bool = True
    light: float = 0.0
    biome: Optional[Biome] = None

    # Percepção
    block_in_front: int = 0  # legado da Etapa 1; use `target` no lugar
    target: Target = field(default_factory=Target)
    blocks: Blocks = field(default_factory=Blocks)
    nearest_entity: Optional[NearestEntity] = None

    inventory: Optional[InventoryState] = None
    container: Optional[ContainerState] = None
    last_result: LastResult = field(default_factory=LastResult)

    # -- parsing --

    @staticmethod
    def from_json(line: str) -> "State":
        return State.from_dict(json.loads(line))

    @staticmethod
    def from_dict(data: dict[str, Any]) -> "State":
        state = State()
        known = {f.name for f in fields(State)}
        for key, value in data.items():
            if key not in known:
                continue  # campo de uma versão mais nova do jogo: ignora
            setattr(state, key, value)

        state.biome = _build(Biome, data.get("biome"))
        state.target = _build(Target, data.get("target")) or Target()
        state.blocks = _build(Blocks, data.get("blocks")) or Blocks()
        state.nearest_entity = _build(NearestEntity, data.get("nearest_entity"))
        state.inventory = _build_inventory(data.get("inventory"))
        state.container = _build_container(data.get("container"))
        state.last_result = _build_last_result(data.get("last_result"))
        return state

    # -- conveniências usadas pela política/curiosidade --

    @property
    def pos(self) -> tuple[float, float, float]:
        return (self.x, self.y, self.z)

    def chunk(self) -> tuple[int, int]:
        """Coordenada de chunk (16x16) — a granularidade da memória espacial.

        `math.floor`, não `int()`: int(-0.5) == 0 arredonda em direção ao
        zero, o que colocaria toda a faixa -1 < x < 0 no chunk 0 em vez do
        -1, e a contagem de visitas de dois chunks vizinhos se misturaria
        justamente em torno da origem.
        """
        return (math.floor(self.x) >> 4, math.floor(self.z) >> 4)

    def is_hurt(self) -> bool:
        return self.health < self.max_health

    def is_starving(self) -> bool:
        return self.food <= 6


def _build(cls: type, data: Any) -> Any:
    """Constrói um dataclass a partir de um dict, ignorando chaves extras."""
    if not isinstance(data, dict):
        return None
    known = {f.name for f in fields(cls)}
    return cls(**{k: v for k, v in data.items() if k in known})


def _build_inventory(data: Any) -> Optional[InventoryState]:
    if not isinstance(data, dict):
        return None
    return InventoryState(
        selected=data.get("selected", 0),
        items=[_build(ItemStack, s) for s in data.get("items", []) if isinstance(s, dict)],
        armor=[_build(ItemStack, s) for s in data.get("armor", []) if isinstance(s, dict)],
        held=_build(ItemStack, data.get("held")),
    )


def _build_container(data: Any) -> Optional[ContainerState]:
    if not isinstance(data, dict):
        return None
    return ContainerState(
        size=data.get("size", 0),
        items=[_build(ItemStack, s) for s in data.get("items", []) if isinstance(s, dict)],
    )


def _build_last_result(data: Any) -> LastResult:
    if not isinstance(data, dict):
        return LastResult()
    # "break" é palavra reservada em Python, então o campo do dataclass é
    # `break_`; a chave no fio continua sendo "break" (o C++ não tem esse
    # problema e o protocolo deve ser legível).
    return LastResult(
        attack=bool(data.get("attack", False)),
        use=bool(data.get("use", False)),
        break_=bool(data.get("break", False)),
        place=bool(data.get("place", False)),
    )


# --- Ação ---------------------------------------------------------------

MOVES = ("forward", "back", "left", "right", "none")
TURNS = ("left", "right", "none")


@dataclass
class Action:
    """Uma ação de um tick — a superfície completa de input de um jogador.

    Movimento aceita duas formas: `move` categórico (política discreta) ou
    `forward`/`strafe` contínuos em -1..1 (política contínua). Se ambos
    estiverem presentes, os contínuos vencem no lado C++.
    """

    # Locomoção
    move: str = "none"
    forward: Optional[float] = None
    strafe: Optional[float] = None
    jump: bool = False
    sneak: bool = False
    sprint: bool = False

    # Mira (graus, relativos ao tick anterior)
    turn: Any = "none"          # "left" | "right" | "none" | float
    look_pitch: float = 0.0

    # Cliques do mouse
    attack: bool = False        # botão esquerdo: bater / minerar
    use: bool = False           # botão direito: usar bloco/entidade/item

    # Atalhos determinísticos (Etapa 1) — agem sobre o alvo mirado
    break_block: bool = False
    place_block: bool = False

    # Inventário
    select_slot: Optional[int] = None
    swap_from: Optional[int] = None
    swap_to: Optional[int] = None
    drop: bool = False          # solta 1 do slot selecionado
    drop_stack: bool = False    # solta a pilha inteira
    drop_all: bool = False      # esvazia o inventário

    # Item em uso / container
    release_item: bool = False  # soltar a corda do arco, terminar de comer
    stop_item: bool = False     # cancelar o uso
    close_container: bool = False

    def to_json(self) -> str:
        # Campos None são omitidos: o lado C++ distingue "campo ausente"
        # (não mexe) de "campo presente" (aplica), e mandar select_slot: null
        # a cada tick reescreveria o slot selecionado para o fallback -1.
        payload = {k: v for k, v in asdict(self).items() if v is not None}
        return json.dumps(payload)

    @staticmethod
    def from_json(line: str) -> "Action":
        data = json.loads(line)
        known = {f.name for f in fields(Action)}
        return Action(**{k: v for k, v in data.items() if k in known})
