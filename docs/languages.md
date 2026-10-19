# Languages and language IDs

The CFW follows the iPod's selected system language. For Apple firmware 2.0.5,
translations use the following language IDs:

| ID | Language |
|---:|---|
| 0 | English |
| 1 | Japanese |
| 2 | Czech |
| 3 | Danish |
| 4 | German |
| 5 | Spanish |
| 6 | French |
| 7 | Greek |
| 8 | Italian |
| 9 | Hungarian |
| 10 | Dutch |
| 11 | Norwegian |
| 12 | Portuguese |
| 13 | Russian |
| 14 | Polish |
| 15 | Finnish |
| 16 | Swedish |
| 17 | Turkish |
| 18 | Korean |
| 19 | Simplified Chinese |
| 20 | Traditional Chinese |

## Translation catalogs

A `.lo` catalog sits alongside its corresponding `.ui` file and shares the same
base name, for example [cfw_info.lo](../payload/cfw_info.lo) for
[cfw_info.ui](../payload/cfw_info.ui). The catalog is loaded automatically during
the build. The `lang` attribute selects the language of a translation:

```xml
<locale>
  <string id="CFW_Info_Title" text="CFW Info"/>
  <string id="CFW_Info_Title" lang="4" text="CFW-Informationen"/>
  <string id="CFW_Info_Title" lang="6" text="Informations sur le CFW"/>
</locale>
```

The UI references the text using `@CFW_Info_Title`. IDs are scoped to each UI
document, including its catalog.

## Default text and fallback

Each text ID requires exactly one declaration **without `lang`**. This provides
the default text, usually in English. `lang="0"` explicitly selects English but
does not replace the required default declaration.

If a translation is missing for the selected language, the default text is used.
This also applies if the system language is unknown or the locale service has not
been initialized. Each language ID may appear only once per text ID; declaration
order does not matter.

## Editing and building

Catalogs are saved as UTF-8. Accented letters and other characters may appear
directly in the text; XML character references are also supported. A language ID
does not imply that all CFW text has already been translated. Changes take effect
in the next firmware build.

The CFW Info page content remains in English; its menu entry and page title can
be translated. Dynamic text uses `<format>` templates. For details on text
references, placeholders, and the C API, see
[UI string references](osos.md#ui-string-references).