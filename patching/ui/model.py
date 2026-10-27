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
    # Optional native follow-up, e.g. navigator.PushScreen with its arguments.
    action: str = ""
    arguments: tuple[str, ...] = ()
    # Optional: bind only on this layout of the screen instead of all of them.
    layout: str | int = ""


@dataclass
class ScreenClone:
    """A new screen reusing a stock screen's controller and one of its layouts."""

    name: str
    screen: str | int
    layout: str | int
    title: str
    # Template instances (by template resource) left out of the cloned layout.
    drop: tuple[str | int, ...] = ()


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
    clones: list[ScreenClone] = field(default_factory=list)
    music_entry: dict[str, str] | None = None
    preview: str = ""
    preview_image: str = ""
    preview_caption: str = ""
    bitmaps: dict[str, tuple[str, str]] = field(default_factory=dict)


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
