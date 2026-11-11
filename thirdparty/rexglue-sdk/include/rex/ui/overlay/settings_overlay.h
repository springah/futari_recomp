/**
 * @file        rex/ui/overlay/settings_overlay.h
 *
 * @brief       ImGui settings overlay dialog for cvar editing with save-to-config.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <rex/ui/imgui_dialog.h>

struct ImFont;

namespace rex::cvar {
struct FlagEntry;
}

namespace rex::ui {

// futari_recomp (2026-11-07): the app's own font for the settings overlay, picked for the display height every
// frame (an ImFont it baked into the drawer's atlas in OnConfigureFonts, at the size it wants). The overlay then
// draws with that font and scales its layout with it, instead of the 10 px base font stretched by overlay_scale.
// A null picker, or nullptr from it, keeps the stretched base font.
using OverlayFontPicker = std::function<ImFont*(float display_height)>;
void SetSettingsOverlayFontPicker(OverlayFontPicker picker);

// futari_recomp (2026-11-10): cvars the app keeps out of the settings overlay. A cvar the filter accepts is not
// listed in simple or advanced mode nor found by search, "Reset all to defaults" leaves it alone, and when it accepts
// "resolution" the Display section's Resolution row is not drawn. A null filter (the default) hides nothing.
using OverlayCvarFilter = std::function<bool(std::string_view name)>;
void SetSettingsOverlayHiddenCvars(OverlayCvarFilter filter);
// futari_recomp (2026-11-11): cvars the overlay lists but "Reset all to defaults" leaves alone. An init-only one's row
// stays disabled, so the overlay can't reset it at all; the app sets it some other way. A null filter (the default)
// keeps nothing back.
void SetSettingsOverlayResetProofCvars(OverlayCvarFilter filter);

class SettingsDialog : public ImGuiDialog {
 public:
  // config_path: where "Save to config" writes (e.g. exe_dir / "app.toml").
  // on_save: optional replacement for the save action -- when set, the Save
  // button invokes it instead of rex::cvar::SaveConfig(config_path), so an app
  // can merge the modified cvars into its own config file format. config_path
  // is still shown as the save destination in the dialog's bottom bar.
  SettingsDialog(ImGuiDrawer* imgui_drawer, std::filesystem::path config_path,
                 std::function<void()> on_save = {});
  ~SettingsDialog();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  // Quick-access display settings (resolution preset, fullscreen, vsync) shown
  // above the generic cvar list when the root category is selected.
  void DrawDisplaySection();

  // Render one settings row into the currently-open 2-column table.
  // DrawCvarRow handles the generic value widget (+ modified marker / Reset);
  // DrawKeybindRow handles key capture, rebind/reset, and conflict detection.
  void DrawCvarRow(const rex::cvar::FlagEntry& entry);
  void DrawKeybindRow(const rex::cvar::FlagEntry& entry);

  std::filesystem::path config_path_;
  std::function<void()> on_save_;
  char search_buf_[128] = {};
  std::string selected_category_;
  std::string capturing_bind_name_;
  // Why the last captured key was rejected (already bound elsewhere); shown
  // below the capture button until a free key is pressed or capture ends.
  std::string capture_conflict_;
};

}  // namespace rex::ui
