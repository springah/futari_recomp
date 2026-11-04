# Renut leg 5: the Windows hands gate

What a person must check before a Renut build ships (leg 4b made it shippable: release table, extract-time
package). Everything below is eyes and hands; the scripted gates (boot, 3-minute idle at 60 fps, 0 shader misses,
package made from the zip) already pass on the release tree. Tick the boxes in this file, or report by line.

Launchers (not in the repo, `out/renut_hands/`): `play_renut.cmd` and `play_xenos.cmd` start the release exe on
your game data, one renderer each. In a zip layout the same thing is `futari.ini` top line `futari_renderer = renut`
or `futari.exe --futari_renderer=renut`. Logs land in `logs/` beside the exe; after a session grep them:

    grep -ciE "error|miss|warn" logs/futari_*.log

## A. Picture, Renut alone

- [ ] Title, PRESS START, mode select, character select: text, portraits, fades (the leg-4 title capture is the ref).
- [ ] Original mode stage 1 to the boss, then Maniac, then Ultra. Bullets, lasers, explosions, the score panel,
      the chain counter, the "NOW LOADING" plates between stages.
- [ ] A game over, name entry, the ranking screen, a continue.
- [ ] Ver 1.5 Arrange modes (the three extra on the mode menu).
- [ ] Black Label DLC when installed (its packages in `dlc\` beside the exe): both its modes boot and draw.
- [ ] Training mode (the panel rows that regressed once in realcomp are a known kind of trouble).
- [ ] Pause menu and the in-game options.

## B. Picture, xenos A/B

Same screens on `play_xenos.cmd`. Things to compare side by side, one screenshot each (Win+Shift+S):
- [ ] Title (colour of the sky gradient, the logo glow).
- [ ] Stage 1 first 10 s (bullet alpha, the ground scroll, the HUD).
- [ ] A boss plate (the big sprites' filtering; CAVE cards are point-filtered on xenos).
- [ ] Any difference you would call a bug, not taste: note the screen and what differs.

## C. Feel

- [ ] Input lag at the title and in stage 1 versus xenos (Renut's whole point: it should feel at least as tight).
- [ ] 60 fps steady in stage 1 and on the boss: the log's fps lines (F3 shows no fps).
- [ ] Audio stays in sync through a stage (the swap counter feeds the renderer; a drift would show as late music cues).

## D. Window

- [ ] F4 overlay opens, is readable, and "Save to config" works on Renut's drawer.
- [ ] F11 (Alt+Enter acts as START) / the fullscreen setting; a resolution change in the ini; the window resized by hand mid-game.
- [ ] Alt-tab out and back during a stage; minimise and restore.
- [ ] Quit from the title (the title-exit binding) and from the game: clean exit, no hang (check the log's last line).

## E. Known and out of scope

- `renut_gpu_backend = d3d12` draws nothing of the game: the package is SPIR-V only. Vulkan is the default (main.cpp); the ini or the flag still wins.
- A missing `vulkan_spirv.bin` is an error line in the log and a black game; the default renderer does not need it.
- Leg 6 is macOS (Metal via plume); nothing here covers it.

## Result

Date, build (commit), renderer-specific defects found (screen, what, how to repeat), and a verdict: ship Renut
opt-in in the next release, or not yet.
