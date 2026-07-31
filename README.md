# Smedley

Smedley is an API and plugin loader for Victoria 2, similar to Minecraft Forge. Smedley plugins can alter and enhance gameplay in ways that base Victoria 2 mods cannot, and have access to real game objects instead of relying on effects/triggers to change the game state.

# The Smedley Kernel

Smedley is made of two components, the bootstrapper (launcher) and kernel. The bootstrapper launches the game, and is responsible for injecting the kernel and the user's selected plugins into the game. The bootstrapper source can be found on the [project page.](https://github.com/shenso/smedley_bootstrapper)

The kernel is responsible for initializing plugins and the facilities used by them. Despite being separate components the bootstrapper will be bundled with releases on this project page.

## V2UP and 3rd party plugin disclaimer

V2UP is the flagship mod of Smedley and comes bundled with it. I intend to add various fixes and improvements to it that still mean to carry on the spirit of game.

Please keep in mind when installing 3rd party plugins that these are not Victoria 2 mods. These are dll files, and are capable of doing anything an exe file can do on your computer when loaded. Please only install and load plugins from sources you trust. I would suggest not using closed-source plugins. I may try to leverage the game's lua runtime or something similar to allow for sandboxing in the future.

## Command-line launcher

`smedley_cli` starts Victoria 2, injects the Smedley kernel and optional plugins,
then resumes the game without using the graphical bootstrapper.

```powershell
smedley_cli --game-dir "C:\path\to\Victoria 2" --detach
smedley_cli --game-dir "C:\path\to\Victoria 2" --plugin plugins/v2up.toml --detach
smedley_cli --game-dir "C:\path\to\Victoria 2" --plugin plugins/campaign_runner.toml --save "C:\path\to\autosave.v2" --detach
smedley_cli --game-dir "C:\path\to\Victoria 2" --plugin plugins/campaign_runner.toml --save "C:\path\to\autosave.v2" --observe --detach
smedley_cli --game-dir "C:\path\to\Victoria 2" --plugin plugins/campaign_runner.toml --plugin plugins/economy_trace.toml --save "C:\path\to\autosave.v2" --detach
```

Use `--dry-run` to validate paths without starting the game. Plugin definition
paths currently cannot contain spaces because the kernel's command-line parser
does not support quoted plugin arguments.

With `--save`, `campaign_runner` uses native GUI dispatch on Victoria 2's
frontend thread to enter Single Player, select the named save through the
normal loader, and enter campaign mode. No mouse or keyboard input is
synthesized. After verifying the in-game idler through RTTI, the runner unpauses
the campaign through Victoria 2's native pause controller.
An optional `--observe` returns the loaded save's player country to native AI
control before unpausing. The current tag remains as a safe UI viewing
perspective, but the runner verifies that no country remains marked as human
controlled and that the former player country has rejoined the AI scheduler.
It then invokes the native `fow` command and verifies full-map visibility before
selecting native speed 5 and unpausing. All nine configurable message popup
dispatchers and their attached pause actions are bypassed only while observer
mode is active; message effects, logs, map notices, and AI processing remain
intact.
If the UI viewing country is annexed, the watchdog pauses, switches the view to
a living AI country through native `tag`, returns that country to AI, verifies
the scheduler, and resumes.

The independent `economy_trace` plugin writes `economy_trace.csv` in the game
directory. Each daily country update records the raw game date, country tag,
treasury, and an adjacent treasury snapshot whose exact purpose is not yet
known. Treasury uses 48.15 fixed point and is divided by 32768 for the displayed
value. Load both plugins when campaign automation and economy output are needed.

See [`mappings/CAMPAIGN_AUTOMATION.md`](mappings/CAMPAIGN_AUTOMATION.md) for
the verified native frontend sequence and its runtime mappings.
