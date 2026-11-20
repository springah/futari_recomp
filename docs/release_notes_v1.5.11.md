**Windows and macOS.** A fix release for v1.5.10: a failed game load could still leave a frozen window behind, and the macOS app no longer records where it was built. Nothing changed in how the game plays.

**What changed since v1.5.10**

- **A failed game load no longer hangs on the way out.** v1.5.10 stopped the crash after the error box (a damaged or incomplete `default.xex`), but about one time in ten the process then stayed behind as a window that had stopped responding (1 of 16 runs of v1.5.10): the SDK's shutdown force-ended its audio thread, and when that thread happened to hold a lock the shutdown waited on it forever. A failed load now ends the process directly, as closing the window already does. Same fix as in the Zuma and Daytona ports.
- **macOS: the app no longer records where it was built.** The executable, the runtime, the eight game-mode libraries and the bundled DirectX Shader Compiler carried the full source paths of the build machine, and the libraries also carried that machine's library directory as a run-path. Source paths now read `./src/...` and the libraries look beside themselves. The Windows build never had them.

How this release was checked: **Windows** — the staging script's checks (no game data in the zip, both renderers boot, the shader translator is built in), then 30 failed loads in a row from the staged build: every one exited, none hung or crashed. **macOS** — signature verified after the zip round-trip, the Renut fallback line, a 25 s boot on the default renderer with no errors (M3 Max), and a shader package made with the rebuilt DirectX Shader Compiler that is byte-identical to the one v1.5.10's makes. The failed-load fix itself was only exercised on Windows; the code is the same on both.

Known limits are those of v1.5.10 (Renut is Vulkan-only and experimental on macOS).

Full details in [CHANGELOG.md](https://github.com/springah/futari_recomp/blob/v1.5.11/CHANGELOG.md).

**Install:** unzip anywhere and double-click `futari.exe` (macOS: `Futari-1.5.11.app`; `RUN.txt` inside has the macOS steps). **Upgrading from v1.5.10:** copy your `assets\`, `dlc\` and `futari.ini` into the new folder and start it; if you use Renut press F2 and "Redo the shaders" once (the shader file names the build that made it).

| File | SHA-256 |
|---|---|
| `Mushihimesama-Futari-PC-v1.5.11.zip` | `5f43e5169b44c4244186ad02ce5695f63405750c68222b191180b2e134fb8dff` |
| `Mushihimesama-Futari-macOS-v1.5.11.zip` | `a26bd6a539631e3cd28812bd4ca7e45da4a2f80da3da3714c7a77b7a7a88ed0f` |
