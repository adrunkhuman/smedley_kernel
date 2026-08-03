# Mapping catalog

The catalog separates executable evidence from reverse-engineering hypotheses.
Evidence notes are intentionally chronological and less polished than user
documentation. They preserve commands, failed probes, hashes, conflicting
observations, and unresolved uncertainty needed to reproduce or audit mapping
claims.

- `verified-runtime`: observed in the supported live game and independently correlated.
- `verified-current`: byte-matched to the cataloged executable and used successfully by current source.
- `verified-static-callsites`: reviewed callers support the proposed semantics without complete runtime coverage.
- `provisional`: useful hypothesis that must not drive default mutation.
- `historical-unverified`: recovered from history and requires runtime verification.
- `historical-skeleton`: only enough structure is known to guide further investigation.

The GUI catalog additionally uses `definition-verified` for names present in
shipped `.gui` files and `verified-static-xref` for names linked to executable
code by reviewed static references.

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

## Evidence

- [`evidence/campaign-automation.md`](evidence/campaign-automation.md)
  documents the verified native frontend sequence used by `--save`.
- [`evidence/interest-payout.md`](evidence/interest-payout.md) records the
  interest-payout investigation and runtime results.
- [`evidence/scripting.md`](evidence/scripting.md) records the scripting
  boundary and acceptance evidence.
- [`evidence/speed-control.md`](evidence/speed-control.md) records native speed
  control, pacing thresholds, and rejected tuning experiments.
- [`evidence/telemetry.md`](evidence/telemetry.md) records telemetry mappings
  and runtime observations.

## Research

- [`research/ai-static-leads.md`](research/ai-static-leads.md) preserves static
  leads recovered from the untrusted Player Military AI package.

`tools/research/find_xrefs.py` finds direct x86 references to ASCII strings,
while `tools/research/find_calls.py` finds direct relative calls to a candidate
RVA. Both are
lightweight triage tools; their nearest-prologue guesses still require manual
disassembly review.
