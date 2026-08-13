# Player Military AI static leads

These are static reverse-engineering leads recovered from the untrusted Player
Military AI 1.9.0 package. The package was never executed or loaded.

## Provenance

- Plugin: `player_military_ai.dll`
- SHA-256: `10bb997cfde542c60f29ab4237ee7d9e6f9fab2c5a1f437f3cd87da6a24d8373`
- Preferred plugin image base: `0x10000000`
- Supported game claimed by the package: Victoria II Heart of Darkness 3.04,
  SHA-256 `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648`

The plugin exports the legacy `MakePlugin` and `PluginName` entry points and is
not compatible with this fork's C plugin ABI. Nothing here implies that its
binaries should be installed or used.

## Country flags

The plugin's helper at plugin RVA `0x33c0` implements the equivalent of:

```cpp
auto *flag = country->_flags.Get(key);
return flag != nullptr && flag->val;
```

Static disassembly shows:

1. Add `0x1b0` to the `CCountry *` to obtain `CFlags *`.
2. Invoke the `CTernary<CFlag *>::Get(const char *)` virtual at vtable offset
   `+0x08`.
3. Return false for a missing flag.
4. Read the boolean at `CFlag+0x1c` for a present flag.

This independently corroborates `CCountry::_flags` at `+0x1b0`, the existing
`Get(const char *)` virtual slot, and `CFlag::val` at `+0x1c`. The complete
object sizes and `CFlag::key` layout still come from the pre-existing headers.
The helper does not identify or require a separate Victoria II
`HasCountryFlag` function.

## Candidate functions

All addresses are game RVAs, not preferred-base virtual addresses. They remain
`historical-unverified` until checked against callers and exercised separately.

| RVA | PMIA interpretation |
| ---: | --- |
| `0x0041fb00` | Land recruitment planner |
| `0x00420760` | Naval recruitment planner |
| `0x0041e280` | Land cleanup |
| `0x0041e5b0` | Naval cleanup |
| `0x004582d0` | Railroad construction selector |
| `0x00458460` | Naval-base construction selector |
| `0x00458560` | Fort construction selector |
| `0x000dad40` | Building-definition registry initializer |
| `0x005b7610` | EAX-state random helper |
| `0x004363f0` | Narrow Great Power sphere manager |
| `0x00437330` | Great Power sphere action selector |

PMIA also names broader routines that it deliberately avoids. These are weaker
search leads because the final DLL does not contain direct references to all of
them:

| RVA | PMIA interpretation |
| ---: | --- |
| `0x0044d1d0` | Colonial planner |
| `0x0041e040` | Recruitment emergency-disband producer |
| `0x004569a0` | Broad production daily routine |
| `0x00433a40` | Broad diplomacy daily routine |
| `0x00436e70` | Neighboring Great Power planner |

## Existing framework overlap

PMIA hooks game RVA `0x001085ae` for one callback per country per day and
validates bytes `53 8b 5d 08 8a 83 bc 15 00 00`. This is the same location
already owned by Smedley's `DailyUpdateEvent`; future plugins should register an
event handler rather than patching the site again.
