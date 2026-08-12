# Contributing

Contributions to Smedley are welcome. For project discussion, join the
[Discord server](https://discord.gg/4SbmmDzNyy).

## Before contributing

Read the repository guidance in [`AGENTS.md`](AGENTS.md), the build instructions
in [`BUILDING.md`](BUILDING.md), and the [styling guide](STYLING.md). Work that
depends on reverse-engineered behavior must also follow the evidence rules in
[`mappings/`](mappings/).

## Resources

- [Smedley documentation](https://shenso.github.io/smedley_kernel/)

## Reporting bugs and issues

Use the repository issue templates. Include the supported executable identity,
the exact command or profile, enabled plugins and mods, reproduction steps,
expected and observed behavior, and relevant logs or trace IDs. Preserve exact
paths, values, and error messages where they help reproduce the problem.

## Testing

Smedley uses GoogleTest and CTest. After building, run:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

This includes the engine ownership audit and its tool tests. For a focused
check while changing target boundaries, run:

```powershell
python tools/check_engine_ownership.py --root .
python tools/tests/check_engine_ownership_test.py
```

## Submitting changes

Keep pull requests focused. State the changed behavior and why it changed,
include targeted validation evidence, and identify compatibility effects,
migrations, untested paths, and remaining limitations. Follow the project style
guide. Open an issue first when a proposed change needs coordination.

## Styling

See the [styling guide](STYLING.md).
