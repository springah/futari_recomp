/**
 * @file        launcher_dialog.h
 * @brief       The launcher: first-run setup and the settings screen, drawn inside futari.exe's own window.
 *
 * futari_recomp (BSD 3-Clause), 2026-11-03. An ImGuiDialog (the SDK's overlay stack) shown from
 * ReXApp::OnFinalizePaths before the game's executable is loaded, when assets/default.xex is missing: the player
 * picks their disc image, console install folder or extracted game folder, the launcher extracts it (game_source),
 * translates the shaders for the Renut renderer (shader_pipeline) and hands the paths back to the SDK ("Play").
 * The same dialog opens over the running game (F2) as the settings screen: renderer, fullscreen, vsync, window
 * size, the DLC found, the shader package, saved into futari.ini for the next start.
 */
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "launcher/file_picker.h"
#include "launcher/game_source.h"
#include "launcher/shader_pipeline.h"

namespace futari::launcher {

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  enum class Mode { kSetup, kSettings };

  struct Env {
    std::filesystem::path exe;
    std::filesystem::path exe_dir;
    std::filesystem::path assets_dir;  // the game data root (assets/ beside the exe unless --game_data_root)
    std::filesystem::path dlc_dir;     // dlc/ beside the exe
    std::string version;               // "1.5.5"
    void* native_window = nullptr;     // for the file dialogs' parent
    bool renut_available = false;      // the build carries the Renut renderer and its translator
    bool in_game = false;              // opened over the running game: no Play, settings apply at the next start
    std::filesystem::path auto_source; // --futari_setup_source: start on this at once and Play when done
    std::function<void()> on_play;     // UI thread; the dialog closes itself right after calling it
    std::function<void()> on_quit;     // UI thread
    std::function<void()> save_display_settings;  // writes the [Display] lines from the cvars (SaveFutariIni)
  };

  // Owned by the drawer, as every ImGuiDialog: it deletes itself on Close().
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, Env env, Mode mode);
  ~LauncherDialog() override;

  static LauncherDialog* instance() { return instance_; }

  // Keyboard and gamepad navigation, and the dialog owns the input while it is up (the game sees an untouched pad
  // over F2): the SDK's ImGuiDrawer feeds SDL3's gamepads to ImGui for dialogs that ask.
  bool WantsGamepadNavigation() const override { return true; }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  enum class Page { kSetup, kWorking, kDone, kSettings };

  void DrawSetup();
  void DrawWorking();
  void DrawDone();
  void DrawSettings();
  void DrawFooterNote();

  void UseSource(const std::filesystem::path& source);
  void StartExtraction(const std::filesystem::path& source);
  void StartShaders();
  void PollWorker();
  void PollPicker();
  void FinishPlay();
  void GoTo(Page page);
  void FocusHere();         // the page's default item: call just before it
  bool BackPressed() const;  // B / Circle, or Esc while no text field is being edited
  // Runs `action` once A / B / Enter / Space / Esc are all released, so the press that closes the dialog or starts
  // the game never reaches the game.
  void AfterRelease(std::function<void()> action);

  void LoadSettings();
  void SaveSettings();
  void RefreshStatus();

  Env env_;
  Mode mode_;
  Page page_ = Page::kSetup;
  Page after_work_ = Page::kDone;
  bool opened_ = false;
  bool auto_started_ = false;
  bool focus_pending_ = true;    // the page changed: focus its default item and the window
  Page settings_return_ = Page::kSetup;  // where B goes from Settings outside the game
  std::function<void()> after_release_;
  bool back_pressed_ = false;   // B or Esc went down this frame
  bool back_was_down_ = false;

  // Setup page state.
  char path_buffer_[1024] = {};
  std::string status_;      // the line under the buttons (an error, a note)
  bool make_shaders_ = true;
  FilePicker picker_;

  // Worker state.
  std::unique_ptr<Progress> progress_;
  std::thread worker_;
  std::atomic<bool> worker_done_{false};
  ExtractResult extract_result_;
  ShaderResult shader_result_;
  bool ran_extract_ = false;
  bool ran_shaders_ = false;
  std::string work_title_;
  std::vector<std::string> log_;
  float fraction_ = -1.0f;

  // Settings page state.
  bool renderer_renut_ = false;
  bool fullscreen_ = false;
  bool vsync_ = true;
  int resolution_index_ = 0;
  std::vector<std::string> dlc_names_;
  std::string package_status_;
  bool package_ok_ = false;
  std::string settings_note_;

  static inline LauncherDialog* instance_ = nullptr;
};

// The top-level "futari_renderer = renut" line in futari.ini (above the first [section], where the SDK reads
// cvars): set it, or remove it for the default renderer. Keeps everything else as it is.
bool WriteRendererLine(const std::filesystem::path& ini_path, bool renut, std::string& error);

}  // namespace futari::launcher
