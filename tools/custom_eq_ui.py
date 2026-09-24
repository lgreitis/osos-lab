"""Build native Custom EQ settings and value selectors."""

import struct

from cfw_info import (
    LEGAL_ITEM,
    LEGAL_PREVIEW,
    LEGAL_PREVIEW_LAYOUT,
    MAIN_SCREEN,
    blocks,
    menu_event,
    pack_blocks,
    put,
    serialized_string,
    word,
)

NOTES_LAYOUT = 0x0DAD09C4
FREQUENCIES = [
    20,
    25,
    31,
    40,
    50,
    60,
    70,
    80,
    100,
    125,
    160,
    200,
    250,
    315,
    400,
    500,
    630,
    800,
    1000,
    1250,
    1600,
    2000,
    2500,
    3150,
    4000,
    5000,
    6300,
    8000,
    10000,
    12500,
    16000,
    20000,
]


def event(name, action, arguments=()):
    return (
        serialized_string(name)
        + b"1"
        + serialized_string("")
        + struct.pack("<I", 1)
        + serialized_string(action)
        + struct.pack("<I", len(arguments))
        + b"".join(serialized_string(arg) for arg in arguments)
    )


def create(resources, firmware):
    prototype = next(
        data
        for _, data in blocks(resources.original["ITEM", 0x41])
        if word(data, 0x30) == LEGAL_ITEM
    )

    sources = {}

    def source(text):
        if text not in sources:
            sources[text] = resources.source(text)
        return sources[text]

    def row(title_source):
        data = bytearray(prototype)
        item_id = resources.allocate()
        put(data, 0x30, item_id)
        put(data, 0x68, title_source)
        return item_id, (0, data)

    def page(name, title, rows, events):
        screen = resources.allocate(name + "_Screen")
        layout = resources.allocate(name + "_Layout")
        table = resources.allocate()
        resources.add("ITEM", table, pack_blocks(rows))
        resources.clone_layout(
            NOTES_LAYOUT, layout, resources.source(title), item_table=table
        )
        resources.add("SCST", screen, resources.original["SCST", MAIN_SCREEN])
        resources.add("CEVT", screen, struct.pack("<I", len(events)) + b"".join(events))
        layouts = blocks(resources.original["SLst", 0x0DAD09C3])
        put(layouts[0][1], 0, layout)
        resources.add("SLst", screen, pack_blocks(layouts))
        resources.added["SEVT", layout] = (
            struct.pack("<I", 2)
            + event("button.menu.up", "navigator.PopTopScreen")
            + event("button.menu.pressandhold", "navigator.PopToMainScreen")
        )
        return table

    def push(item_id, name, handler=""):
        return menu_event(
            item_id,
            "chosen",
            handler,
            "navigator.PushScreen",
            [name + "_Screen", name + "_Layout", "foregroundpush"],
        )

    fields = []
    definitions = []

    def array(name, values, public=False):
        values_c = ", ".join(f"{value:#x}" for value in values)
        prefix = "" if public else "static "
        definitions.append(f"{prefix}const uint32_t {name}[] = {{{values_c}}};")
        return name

    def selector(name, title, values, default):
        field_id = len(fields)
        labels = [source(value) for value in values]
        marked = [source(value + " *") for value in values]
        summaries = [source(title + ": " + value) for value in values]
        rows, events, items = [], [], []
        for index, label in enumerate(labels):
            item_id, item = row(marked[index] if index == default else label)
            items.append(item_id)
            rows.append(item)
            events.append(
                menu_event(
                    item_id,
                    "chosen",
                    f"CFW_EQ_Set_{field_id:02d}_{index:02d}",
                    "navigator.PopTopScreen",
                    [],
                )
            )
        table = page(name, title, rows, events)
        parent_item, parent_row = row(summaries[default])
        arrays = [
            array(f"eq_field_{field_id}_{key}", data)
            for key, data in (
                ("labels", labels),
                ("marked", marked),
                ("summaries", summaries),
                ("items", items),
            )
        ]
        fields.append(
            {
                "parent_item": parent_item,
                "selector_table": table,
                "count": len(values),
                "default": default,
                "arrays": arrays,
            }
        )
        return parent_row, push(parent_item, name)

    precut_values = ["0.0 dB"] + [f"-{value / 2:.1f} dB" for value in range(1, 21)]
    precut_row, precut_event = selector("CFW_EQ_Precut", "Precut", precut_values, 0)
    rows, events = [precut_row], [precut_event]
    for band, (kind, frequency) in enumerate(((2, 60), (1, 1000), (3, 8000)), 1):
        name = f"CFW_EQ_Band{band}"
        item_id, item = row(source(f"Band {band}"))
        rows.append(item)
        events.append(push(item_id, name))
        band_rows, band_events = [], []
        first_field = len(fields)
        for key, title, values, default in (
            ("Type", "Type", ["Off", "Peaking", "Low shelf", "High shelf"], kind),
            (
                "Frequency",
                "Frequency",
                [f"{hz} Hz" for hz in FREQUENCIES],
                FREQUENCIES.index(frequency),
            ),
            ("Q", "Q", [f"{q / 10:.1f}" for q in range(3, 41)], 4),
            (
                "Gain",
                "Gain",
                [
                    f"{gain / 2:+.1f} dB" if gain else "0.0 dB"
                    for gain in range(-24, 25)
                ],
                24,
            ),
        ):
            item, chosen = selector(name + "_" + key, title, values, default)
            band_rows.append(item)
            band_events.append(chosen)
        table = page(name, f"Band {band}", band_rows, band_events)
        for index, field in enumerate(fields[first_field:]):
            field.update(parent_table=table, parent_index=index)
    save_item, save_row = row(source("Save settings"))
    rows.append(save_row)
    events.append(
        menu_event(
            save_item,
            "chosen",
            "CFW_EQ_Save",
            "navigator.SwitchLayout",
            ["CFW_EQ_Layout"],
        )
    )
    main_table = page("CFW_EQ", "Custom EQ", rows, events)
    fields[0].update(parent_table=main_table, parent_index=0)

    # Extend Apple's native preset tables with menu index 23 / stored ID 122.
    preset_string = resources.allocate()
    resources.add("Str ", preset_string, b"Custom\0")
    preset_resources = list(
        struct.unpack_from("<46I", firmware, 0x089CC500 - 0x08000000 + 0xB6D8)
    )
    if preset_resources[:2] != [0x0DAD0B7F, 0x0DAD0BE0]:
        raise ValueError("Unexpected native EQ name and preview table")
    preset_resources.extend([preset_string, preset_resources[8 * 2 + 1]])
    array("cfw_eq_preset_resources", preset_resources, public=True)
    array("cfw_eq_preset_ids", [0, *range(100, 123)], public=True)

    title = resources.source("Custom EQ")
    item_id, item = row(title)
    items = blocks(resources.added["ITEM", 0x41])
    eq_index = next(
        i for i, (_, data) in enumerate(items) if word(data, 0x30) == 0x0DAD0C39
    )
    items.insert(eq_index + 1, item)
    resources.added["ITEM", 0x41] = pack_blocks(items)

    preview = resources.allocate("CFW_EQ_Preview")
    preview_view = resources.allocate()
    data = bytearray(resources.original["VLyt", LEGAL_PREVIEW_LAYOUT])
    put(data, 0x24, title)
    resources.add("VLyt", preview_view, data)
    resources.add(
        "TEVT", preview_view, resources.original["TEVT", LEGAL_PREVIEW_LAYOUT]
    )
    resources.clone_layout(
        LEGAL_PREVIEW, preview, substitutions={LEGAL_PREVIEW_LAYOUT: preview_view}
    )
    layouts = blocks(resources.added["SLst", MAIN_SCREEN])
    kind, data = next(
        (kind, bytearray(data))
        for kind, data in layouts
        if word(data, 0) == LEGAL_PREVIEW
    )
    put(data, 0, preview)
    layouts.append((kind, data))
    resources.added["SLst", MAIN_SCREEN] = pack_blocks(layouts)
    events = bytearray(resources.added["CEVT", MAIN_SCREEN])
    put(events, 0, word(events, 0) + 2)
    events.extend(push(item_id, "CFW_EQ", "CFW_EQ_Open"))
    events.extend(
        menu_event(
            item_id,
            "delayedselected",
            "ShowSetting_Legal",
            "navigator.SwitchLayout",
            ["CFW_EQ_Preview"],
        )
    )
    resources.added["CEVT", MAIN_SCREEN] = bytes(events)

    definitions.extend(
        [
            f"const uint32_t cfw_eq_main_table = {main_table:#x};",
            f"const uint32_t cfw_eq_save_item = {save_item:#x};",
        ]
    )
    array("cfw_eq_frequencies", FREQUENCIES, public=True)
    array(
        "cfw_eq_save_labels",
        [
            source(text)
            for text in (
                "Save settings",
                "Save failed - retry",
                "Invalid filter settings",
            )
        ],
        public=True,
    )
    definitions.append("const struct cfw_eq_field cfw_eq_fields[CFW_EQ_FIELDS] = {")
    for field in fields:
        numbers = [
            field[key]
            for key in (
                "parent_table",
                "parent_index",
                "parent_item",
                "selector_table",
                "count",
                "default",
            )
        ]
        values = [f"{value:#x}" for value in numbers] + field["arrays"]
        definitions.append("    {" + ", ".join(values) + "},")
    definitions.append("};")
    return definitions
