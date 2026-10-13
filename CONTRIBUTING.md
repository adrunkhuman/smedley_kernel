# Contributing

Contributions to Smedley are welcome. For project discussion, join the
[Discord server](https://discord.gg/4SbmmDzNyy).

## Workflow

Read [repository guidance](AGENTS.md) and the
[development guide](docs/development.md) for build, install, testing, and C++
style conventions. Reverse-engineered behavior must follow the
[mapping evidence rules](mappings/README.md).

Contributions use the destination component's license; see the
[license map](LICENSE.md). Preserve inherited and third-party notices. Moving
code into an MIT-0 directory does not relicense it.

Keep pull requests focused. Open an issue first when a proposed change needs
coordination. State the changed behavior and why it changed; include validation
evidence and identify compatibility effects, migrations, untested paths, and
remaining limitations. Run the [complete test suite](docs/development.md#testing)
and relevant boundary checks. Host tests do not establish live-game behavior;
follow the [validation limits](docs/development.md#validation-limits).

## Reporting bugs and issues

Use the repository issue templates. Include the supported executable identity,
the exact command or profile, enabled plugins and mods, reproduction steps,
expected and observed behavior, and relevant logs or trace IDs. Preserve exact
paths, values, and error messages where they help reproduce the problem.

## Resources

- [Project overview and documentation index](README.md)
- [Smedley documentation](https://shenso.github.io/smedley_kernel/)
