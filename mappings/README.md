# Mapping catalog

The catalog separates executable evidence from reverse-engineering hypotheses.

- `verified-current`: used by current source and byte-matched to the cataloged executable.
- `verified-historical`: byte-matched, but its meaning comes from removed historical code.
- `current-unverified`: present in current source but not dynamically verified here.
- `historical-unverified`: recovered from history and requires runtime verification.
- `historical-skeleton`: only enough structure is known to guide further investigation.

Validate the executable and known instruction signatures before using an address:

```powershell
python tools/validate_mappings.py "C:\path\to\Victoria 2\v2game.exe"
```

An exact byte match proves that the expected code is present at an address. It
does not prove a proposed name, field type, calling convention, or safe hook
boundary. Those require decompiler analysis and runtime observation.

`gui-controls-3.04.toml` records stable names from the game's `.gui`
definitions. These names are useful anchors for finding runtime GUI objects,
but they do not yet identify the engine handlers behind each control.

`CAMPAIGN_AUTOMATION.md` documents the verified native frontend sequence used
by `--save`; it does not rely on mouse or keyboard input.

`SPEED_CONTROL.md` documents the native speed index, pacing thresholds, and
intra-day application service checkpoints. It also records rejected runtime
tuning experiments so they are not repeated as prospective optimizations.

`tools/find_xrefs.py` finds direct x86 references to ASCII strings, while
`tools/find_calls.py` finds direct relative calls to a candidate RVA. Both are
lightweight triage tools; their nearest-prologue guesses still require manual
disassembly review.
