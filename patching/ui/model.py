# SPDX-License-Identifier: GPL-3.0-only
"""UI document nodes and the native bindings produced by compilation."""

from dataclasses import dataclass, field


@dataclass
class Item:
    kind: str
    name: str
    title: str
    text_file: str = ""
    select: str = "return"
    layout: str = "inline"
    factory: str = ""
    view_type: str = ""


@dataclass
class ScreenEvent:
    screen: str | int
    event: str
    handler: str
    kind: str = "CEVT"  # SEVT extends a layout instead of a screen.


@dataclass
class Document:
    root: Item | None
    after: str | int = ""
    open_action: str = ""
    strings: dict[str, str] = field(default_factory=dict)
    events: list[ScreenEvent] = field(default_factory=list)
    preview: str = ""
    preview_image: str = ""
    preview_caption: str = ""
    bitmaps: dict[str, tuple[str, str]] = field(default_factory=dict)


@dataclass
class CompiledDocument:
    string_ids: dict[str, int] = field(default_factory=dict)
