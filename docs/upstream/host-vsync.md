# Host presentation never waited for vblank (`present_vsync`)

Draft for the ReXGlue SDK. The change is in the vendored tree under
`thirdparty/rexglue-sdk`; this note describes it for an upstream PR.

## Symptom

A player with a fixed 60Hz monitor reported a tear line near the top of the
screen and an external frame counter reading about 120 FPS, with `vsync = true`
(springah/futari_recomp#5). The log was clean and the ini was correct.

## Cause

The GPU `vsync` cvar (`src/graphics/command_processor.cpp`) only paces the
guest vblank interrupt in `GraphicsSystem`'s vsync worker. Nothing fed it to
the host side:

- `D3D12Presenter::PaintAndPresentImpl` always called
  `Present(0, DXGI_PRESENT_RESTART | DXGI_PRESENT_ALLOW_TEARING)` when DXGI
  supports tearing, which it does on every current Windows build. The comment
  there explains the intent (variable refresh rate in borderless fullscreen,
  host refresh rates that are not multiples of the guest's), but it leaves a
  fixed-refresh display with no way to avoid tearing: the app opts in, so the
  driver's own VSync setting cannot override it.
- `VulkanPresenter` prefers `IMMEDIATE`, then `MAILBOX`, then `FIFO_RELAXED`,
  two of which can tear.
- The 120 FPS reading is the UI thread: while any `ImGuiDialog` is alive the
  drawer requests a repaint every host vblank (`ImGuiDrawer::Draw` ->
  `RequestUIPaintFromUIThread`), and each guest frame also forces a present, so
  roughly 2 presents per 60Hz frame, each immediate.

## Change

- New cvar `present_vsync` (bool, default `true`, `UI/Presenter`), defined in
  `src/ui/presenter.cpp` and declared in `include/rex/ui/flags.h`.
- D3D12: `Present(present_vsync ? 1 : 0, DXGI_PRESENT_RESTART | (tearing flag
  only when !present_vsync && swap_chain_allows_tearing))`. The swap chain is
  still created with `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`, which only permits
  the per-present flag, so the cvar can toggle at runtime without recreating
  it. `DXGI_PRESENT_RESTART` with sync interval 1 drops a queued UI re-present
  of the previous guest frame when a new guest frame arrives, so guest frames
  are not delayed by a vblank behind it.
- D3D12 reports `is_vsync_implicit_out = present_vsync` from
  `ConnectOrReconnectPaintingToSurfaceFromUIThread`, so `Presenter` paints
  from the UI thread on request (as it already does for a FIFO Vulkan
  swapchain) rather than letting the guest output thread block in `Present`
  once the present queue fills.
- Vulkan: with `present_vsync`, skip `IMMEDIATE` and `FIFO_RELAXED`; `MAILBOX`
  (no tearing, drops stale frames) then `FIFO`. Takes effect at swapchain
  (re)creation.
- `SettingsDialog`'s VSync checkbox sets both `vsync` and `present_vsync`.

## Open questions for upstream

- Whether the default should be `true` (no tearing, what the word VSync means
  to players) or `false` (current behaviour, better for VRR). This port ships
  `true`; VRR users set `present_vsync = false`.
- A runtime toggle of `present_vsync` changes the D3D12 `Present` call at once
  but the UI/guest thread arrangement only at the next surface reconnect. A
  `SetPaintModeFromUIThread` refresh on cvar change would close that gap.
- Whether the UI thread's per-vblank repaint should skip presenting when the
  guest output has not changed and no dialog is visible, to avoid the doubled
  present rate in the `present_vsync = false` case too.
