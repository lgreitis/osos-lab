# SPDX-License-Identifier: GPL-3.0-only
"""UI document nodes and the native bindings produced by compilation."""

from dataclasses import dataclass, field


@dataclass
class Values:
    name: str
    numbers: list[int] = field(default_factory=list)
    labels: list[str] = field(default_factory=list)


@dataclass
class Item:
    kind: str
    name: str
    title: str
    choices: str = ""
    slot: int = 0
    default_index: int = 0
    text_file: str = ""
    items: list["Item"] = field(default_factory=list)


@dataclass
class ScreenEvent:
    screen: str | int
    event: str
    handler: str


@dataclass
class Document:
    root: Item | None
    values: dict[str, Values]
    fields: list[Item]
    actions: list[Item]
    after: str | int = ""
    open_action: str = ""
    strings: dict[str, str] = field(default_factory=dict)
    events: list[ScreenEvent] = field(default_factory=list)


@dataclass
class FieldBinding:
    parent_item: int
    selector_table: int
    default: int
    labels: list[int]
    marked: list[int]
    summaries: list[int]
    items: list[int]
    parent_table: int = 0
    parent_index: int = 0


@dataclass
class ActionBinding:
    item: int
    index: int
    table: int = 0
    labels: list[int] = field(default_factory=list)


@dataclass
class CompiledDocument:
    fields: dict[int, FieldBinding] = field(default_factory=dict)
    actions: dict[str, ActionBinding] = field(default_factory=dict)
    string_ids: dict[str, int] = field(default_factory=dict)
