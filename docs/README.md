# Firmware references

The reference addresses below describe Classic 7G Rev B / Apple firmware 2.0.4.
Active builds use 2.0.5; do not apply historical addresses to it. Addresses are
version-specific. Reverse-engineered details describe static findings; they do
not establish device behavior for new changes.

- [Boot sequence](boot.md): system selection and Apple handoff.
- [NOR bootloader](nor.md): firmware volume, drivers and image loading.
- [OSOS runtime](osos.md): relocation, memory, storage and audio.

Input fingerprints and analysis are retained in [firmware](../firmware/) and [analysis profiles](../ghidra/profiles/) and
[ghidra/analysis](../ghidra/analysis/).
