/**
 * @file        launcher_dialog.cpp
 * @brief       The launcher's screens (see the header). UI thread throughout; the work runs on worker_.
 */
#include "launcher/launcher_dialog.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include <imgui.h>
#include "launcher/launcher_font.h"
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

namespace futari::launcher {

namespace {

std::string Utf8(const std::filesystem::path& p) { return rex::path_to_utf8(p); }

// Baked once (the SDK's renderer backends upload the atlas once; ImGui 1.92 without RendererHasTextures scales a
// font only by stretching its bitmap), so a ladder of sizes covers 720p to 2160p crisply.
constexpr float kFontSizes[] = {15.0f, 18.0f, 22.0f, 27.0f, 33.0f, 40.0f, 50.0f};
ImFont* g_fonts[std::size(kFontSizes)] = {};

ImFont* PickFont(float display_height) {
  const float want = display_height / 36.0f;  // 30 px at 1080 lines -> the 27 px font; 20 at 720 -> 18
  ImFont* best = nullptr;
  for (size_t i = 0; i < std::size(kFontSizes); ++i) {
    if (g_fonts[i] && (!best || kFontSizes[i] <= want)) {
      best = g_fonts[i];
    }
  }
  return best;
}

struct Resolution {
  const char* label;
  int w, h;
};
constexpr Resolution kResolutions[] = {
    {"1280 x 720", 1280, 720},   {"1600 x 900", 1600, 900},   {"1920 x 1080", 1920, 1080},
    {"2560 x 1440", 2560, 1440}, {"3840 x 2160", 3840, 2160},
};

std::string Trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

bool IsXContentFile(const std::filesystem::path& p) {
#ifdef _WIN32
  FILE* f = _wfopen(p.c_str(), L"rb");
#else
  FILE* f = std::fopen(p.c_str(), "rb");
#endif
  if (!f) {
    return false;
  }
  char magic[4] = {};
  const size_t got = std::fread(magic, 1, 4, f);
  std::fclose(f);
  return got == 4 && (std::memcmp(magic, "LIVE", 4) == 0 || std::memcmp(magic, "CON ", 4) == 0 ||
                      std::memcmp(magic, "PIRS", 4) == 0);
}

void WrappedText(const std::string& text) { ImGui::TextWrapped("%s", text.c_str()); }

// The key's state as the drawer fed it, whatever ImGui's key ownership says: ImGui's own navigation takes Esc and
// the gamepad's B for "cancel" and IsKeyPressed then answers false to everyone else.
bool RawDown(ImGuiKey key) { return ImGui::GetIO().KeysData[key - ImGuiKey_NamedKey_BEGIN].Down; }

}  // namespace

void LauncherDialog::AddFonts(ImFontAtlas* atlas) {
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D,
                                    0x2022, 0x2022, 0x2026, 0x2026, 0};
  for (size_t i = 0; i < std::size(kFontSizes); ++i) {
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    std::snprintf(config.Name, sizeof(config.Name), "Roboto Medium %.0fpx (launcher)", kFontSizes[i]);
    g_fonts[i] = atlas->AddFontFromMemoryCompressedTTF(futari_launcher_font_compressed_data,
                                                       int(futari_launcher_font_compressed_size), kFontSizes[i],
                                                       &config, kRanges);
  }
}

bool WriteRendererLine(const std::filesystem::path& ini_path, bool renut, std::string& error) {
  std::string original;
  {
    std::ifstream in(ini_path, std::ios::binary);
    if (in) {
      std::ostringstream buf;
      buf << in.rdbuf();
      original = buf.str();
    }
  }
  const bool crlf = original.find("\r\n") != std::string::npos;
  const std::string eol = crlf ? "\r\n" : "\n";
  std::vector<std::string> lines;
  {
    std::istringstream in(original);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      lines.push_back(line);
    }
  }
  // Drop every top-level futari_renderer line (above the first [section]).
  std::vector<std::string> kept;
  bool in_section = false;
  for (const auto& line : lines) {
    const std::string t = Trim(line);
    if (!t.empty() && t.front() == '[') {
      in_section = true;
    }
    if (!in_section) {
      const size_t eq = t.find('=');
      if (eq != std::string::npos && Trim(t.substr(0, eq)) == "futari_renderer") {
        continue;
      }
    }
    kept.push_back(line);
  }
  if (renut) {
    // At the very top, where RUN.txt tells the player to put it by hand.
    kept.insert(kept.begin(), "futari_renderer = renut");
  }
  std::string merged;
  for (const auto& line : kept) {
    merged += line;
    merged += eol;
  }
  const std::filesystem::path tmp = ini_path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "cannot write " + Utf8(tmp);
      return false;
    }
    out << merged;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, ini_path, ec);
  if (ec) {
    error = "cannot replace " + Utf8(ini_path) + ": " + ec.message();
    return false;
  }
  return true;
}

LauncherDialog::LauncherDialog(rex::ui::ImGuiDrawer* drawer, Env env, Mode mode)
    : ImGuiDialog(drawer), env_(std::move(env)), mode_(mode) {
  instance_ = this;
  page_ = mode == Mode::kSettings ? Page::kSettings : Page::kSetup;
  if (mode == Mode::kSetup && env_.auto_source.empty()) {
    // --futari_setup over a game that is already in place: the Done page (Play / Settings / Quit), not the
    // "this build contains no game data" page (seen 2026-11-04 on macOS).
    std::error_code ec;
    if (std::filesystem::is_regular_file(env_.assets_dir / "default.xex", ec)) {
      page_ = Page::kDone;
    }
  }
  make_shaders_ = env_.renut_available;
  if (mode == Mode::kSettings) {
    LoadSettings();
  }
  RefreshStatus();
}

LauncherDialog::~LauncherDialog() {
  if (worker_.joinable()) {
    if (progress_) {
      progress_->cancel.store(true);
    }
    worker_.join();
  }
  if (instance_ == this) {
    instance_ = nullptr;
  }
}

void LauncherDialog::OnClose() {
  if (instance_ == this) {
    instance_ = nullptr;
  }
}

void LauncherDialog::OnDraw(ImGuiIO& io) {
  if (!auto_started_ && !env_.auto_source.empty() && mode_ == Mode::kSetup) {
    auto_started_ = true;
    UseSource(env_.auto_source);
  }
  // The setup flow over a game that is already there (--futari_setup) and a package another build made: redo it
  // now, the game would otherwise miss shaders on Renut. The Done page does the same after an extraction.
  if (!stale_redo_started_ && mode_ == Mode::kSetup && env_.auto_source.empty() &&
      (page_ == Page::kSetup || page_ == Page::kDone) && package_stale_ && env_.renut_available &&
      !worker_.joinable()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(env_.assets_dir / "default.xex", ec)) {
      stale_redo_started_ = true;
      REXLOG_WARN("Launcher: the Renut shaders were made by a different build; redoing them");
      after_work_ = Page::kDone;
      StartShaders();
    }
  }
  PollPicker();
  PollWorker();
  {
    const bool back_down = RawDown(ImGuiKey_GamepadFaceRight) || RawDown(ImGuiKey_Escape);
    // Esc inside the path field only leaves the field (ImGui's own handling): the text field is the only item that
    // stays active across frames here.
    back_pressed_ = back_down && !back_was_down_ && !ImGui::IsAnyItemActive();
    back_was_down_ = back_down;
  }
  if (after_release_) {
    bool held = false;
    for (ImGuiKey key : {ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight, ImGuiKey_Enter, ImGuiKey_KeypadEnter,
                         ImGuiKey_Space, ImGuiKey_Escape}) {
      held = held || RawDown(key);
    }
    if (!held) {
      auto action = std::move(after_release_);
      after_release_ = nullptr;
      action();
      return;  // the action may have closed the dialog
    }
  }

  ImFont* font = PickFont(io.DisplaySize.y);
  const float font_px = font ? font->LegacySize : 20.0f;
  if (font) {
    ImGui::PushFont(font, font->LegacySize);
  }
  const ImVec2 size(std::min(io.DisplaySize.x - 40.0f, 46.0f * font_px), std::min(io.DisplaySize.y - 40.0f, 27.0f * font_px));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(env_.in_game ? 0.92f : 1.0f);
  if (focus_pending_) {
    ImGui::SetNextWindowFocus();  // over the game, F2 must take the keyboard and the pad at once
  }
  const std::string title = "Mushihimesama Futari (recompiled) v" + env_.version + "##futari_launcher";
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
  bool open = true;
  if (ImGui::Begin(title.c_str(), env_.in_game ? &open : nullptr, kFlags)) {
    if (!font) {
      ImGui::SetWindowFontScale(io.DisplaySize.y >= 1000.0f ? 2.0f : 1.5f);  // no launcher font: ProggyTiny, as before
    }
    switch (page_) {
      case Page::kSetup:
        DrawSetup();
        break;
      case Page::kWorking:
        DrawWorking();
        break;
      case Page::kDone:
        DrawDone();
        break;
      case Page::kSettings:
        DrawSettings();
        break;
    }
  }
  ImGui::End();
  if (font) {
    ImGui::PopFont();
  }
  if (!open) {
    Close();
  }
}

void LauncherDialog::GoTo(Page page) {
  if (page == Page::kSettings && page_ != Page::kSettings && page_ != Page::kWorking) {
    settings_return_ = page_;
  }
  page_ = page;
  focus_pending_ = true;
}

void LauncherDialog::FocusHere() {
  if (focus_pending_) {
    focus_pending_ = false;
    ImGui::SetKeyboardFocusHere();
  }
}

bool LauncherDialog::BackPressed() const {
  if (after_release_) {
    return false;
  }
  return back_pressed_;
}

void LauncherDialog::AfterRelease(std::function<void()> action) {
  if (!after_release_) {
    after_release_ = std::move(action);
  }
}

void LauncherDialog::DrawFooterNote() {
  ImGui::Spacing();
  ImGui::TextDisabled("Game data goes into %s and %s. Nothing leaves this folder.",
                      Utf8(env_.assets_dir.filename()).c_str(), Utf8(env_.dlc_dir.filename()).c_str());
}

void LauncherDialog::DrawSetup() {
  WrappedText("This build contains no game data. It runs from your own copy of Mushihimesama Futari Ver 1.5 "
              "(Xbox 360). Pick what you have:");
  ImGui::Spacing();
  const bool busy = picker_.pending();
  ImGui::BeginDisabled(busy);
  FocusHere();
  if (ImGui::Button("Disc image (.iso)...")) {
    picker_.Open(env_.native_window, false, "Pick your Mushihimesama Futari disc image", "iso");
  }
  ImGui::SameLine();
  if (ImGui::Button("Console install folder...")) {
    picker_.Open(env_.native_window, true, "Pick the folder copied off your console (435A07D7 or above it)", "");
  }
  ImGui::SameLine();
  if (ImGui::Button("Extracted game folder...")) {
    picker_.Open(env_.native_window, true, "Pick the folder with default.xex and media in it", "");
  }
  ImGui::Spacing();
  ImGui::TextUnformatted("Or type a path:");
  ImGui::SetNextItemWidth(-90.0f * (ImGui::GetFontSize() / 10.0f) / 2.0f);
  const bool entered = ImGui::InputText("##path", path_buffer_, sizeof(path_buffer_),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if (ImGui::Button("Use") || entered) {
    const std::string typed = Trim(path_buffer_);
    if (!typed.empty()) {
      UseSource(rex::to_path(typed));
    }
  }
  ImGui::EndDisabled();
  ImGui::Spacing();
  if (env_.renut_available) {
    ImGui::Checkbox("Also prepare the Renut renderer", &make_shaders_);
    ImGui::TextDisabled("  (optional; the game opens and closes twice by itself, ~15 s)");
  }
  if (!status_.empty()) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.45f, 1.0f));
    WrappedText(status_);
    ImGui::PopStyleColor();
  }
  DrawFooterNote();
  ImGui::Separator();
  if (ImGui::Button("Quit")) {
    if (env_.on_quit) {
      env_.on_quit();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Settings")) {
    LoadSettings();
    RefreshStatus();
    GoTo(Page::kSettings);
  }
}

void LauncherDialog::DrawWorking() {
  ImGui::TextUnformatted(work_title_.c_str());
  ImGui::Spacing();
  const float footer = ImGui::GetFrameHeightWithSpacing() * 2.5f;
  if (ImGui::BeginChild("##log", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
    const size_t first = log_.size() > 200 ? log_.size() - 200 : 0;
    for (size_t i = first; i < log_.size(); ++i) {
      ImGui::TextUnformatted(log_[i].c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeight() * 2.0f) {
      ImGui::SetScrollHereY(1.0f);
    }
  }
  ImGui::EndChild();
  if (fraction_ >= 0.0f) {
    ImGui::ProgressBar(fraction_, ImVec2(-1, 0));
  } else {
    // Indeterminate: a sliding block.
    const float t = float(ImGui::GetTime());
    ImGui::ProgressBar(-1.0f * t, ImVec2(-1, 0), "working...");
  }
  FocusHere();
  if (ImGui::Button("Cancel") || BackPressed()) {
    if (progress_) {
      progress_->cancel.store(true);
    }
  }
}

void LauncherDialog::DrawDone() {
  const bool extract_ok = !ran_extract_ || extract_result_.status == ExtractResult::Status::kOk;
  if (ran_extract_) {
    if (extract_ok) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.6f, 1.0f));
      WrappedText(extract_result_.message);
      ImGui::PopStyleColor();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.5f, 1.0f));
      WrappedText(extract_result_.status == ExtractResult::Status::kDlcOnly ? "DLC only: " + extract_result_.message
                                                                              : "Failed: " + extract_result_.message);
      ImGui::PopStyleColor();
      if (!extract_result_.problems.empty()) {
        const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f;
        if (ImGui::BeginChild("##problems", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
          for (const auto& p : extract_result_.problems) {
            ImGui::TextUnformatted(p.c_str());
          }
        }
        ImGui::EndChild();
      }
    }
  }
  if (ran_shaders_) {
    ImGui::Spacing();
    if (shader_result_.ok) {
      WrappedText(shader_result_.message);
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.45f, 1.0f));
      WrappedText("Renut shaders: " + shader_result_.message +
                  " (the default renderer runs; try again from Settings later)");
      ImGui::PopStyleColor();
    }
  }
  ImGui::Spacing();
  ImGui::Separator();
  std::error_code ec;
  const bool have_game = std::filesystem::is_regular_file(env_.assets_dir / "default.xex", ec);
  const bool can_play = have_game && !env_.in_game && env_.on_play;
  if (can_play) {
    FocusHere();
    if (ImGui::Button("Play")) {
      AfterRelease([this] { FinishPlay(); });
    }
    ImGui::SameLine();
  } else {
    FocusHere();
  }
  if (ImGui::Button(have_game ? "Settings" : "Back") || (!have_game && BackPressed())) {
    if (have_game) {
      LoadSettings();
      RefreshStatus();
      GoTo(Page::kSettings);
    } else {
      status_.clear();
      GoTo(Page::kSetup);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Quit")) {
    if (env_.on_quit) {
      env_.on_quit();
    }
  }
}

void LauncherDialog::DrawSettings() {
  std::error_code ec;
  const bool have_game = std::filesystem::is_regular_file(env_.assets_dir / "default.xex", ec);
  if (env_.renut_available) {
    ImGui::TextUnformatted("Renderer");
    ImGui::SameLine();
    FocusHere();
    if (ImGui::RadioButton("Default", !renderer_renut_)) {
      renderer_renut_ = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Renut (Vulkan)", renderer_renut_)) {
      renderer_renut_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("applies at the next start");
    if (package_stale_) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.45f, 1.0f));
      ImGui::TextUnformatted(package_status_.c_str());
      ImGui::PopStyleColor();
    } else {
      ImGui::TextUnformatted(package_status_.c_str());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!have_game || worker_.joinable());
    if (ImGui::Button(package_ok_ || package_stale_ ? "Redo the shaders" : "Prepare the shaders")) {
      after_work_ = Page::kSettings;
      StartShaders();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
  }
  FocusHere();  // no-op when the renderer row took the focus
  ImGui::Checkbox("Fullscreen (F11 toggles it in the game)", &fullscreen_);
  ImGui::Checkbox("VSync", &vsync_);
  ImGui::TextUnformatted("Window size");
  ImGui::SameLine();
  std::vector<std::string> labels;
  labels.push_back("current (" + std::to_string(rex::cvar::Query<int32_t>("window_width")) + " x " +
                   std::to_string(rex::cvar::Query<int32_t>("window_height")) + ")");
  for (const auto& r : kResolutions) {
    labels.push_back(r.label);
  }
  std::vector<const char*> items;
  for (const auto& l : labels) {
    items.push_back(l.c_str());
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
  ImGui::Combo("##resolution", &resolution_index_, items.data(), int(items.size()));
  ImGui::Spacing();
  if (dlc_names_.empty()) {
    ImGui::TextDisabled("DLC: none in %s (put your Xbox 360 DLC packages there, or pick a folder holding them in Setup)",
                        Utf8(env_.dlc_dir.filename()).c_str());
  } else {
    ImGui::Text("DLC in %s:", Utf8(env_.dlc_dir.filename()).c_str());
    for (const auto& n : dlc_names_) {
      ImGui::BulletText("%s", n.c_str());
    }
  }
  ImGui::Spacing();
  DrawUpdateCheck();
  if (!settings_note_.empty()) {
    ImGui::Spacing();
    WrappedText(settings_note_);
  }
  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button(env_.in_game ? "Save and close" : "Save")) {
    SaveSettings();
    if (env_.in_game) {
      AfterRelease([this] { Close(); });
    }
  }
  ImGui::SameLine();
  if (env_.in_game) {
    if (ImGui::Button("Close") || BackPressed()) {
      AfterRelease([this] { Close(); });
    }
  } else {
    if (have_game && env_.on_play) {
      if (ImGui::Button("Play")) {
        AfterRelease([this] { FinishPlay(); });
      }
      ImGui::SameLine();
    }
    if (ImGui::Button("Setup")) {
      GoTo(Page::kSetup);
    } else if (BackPressed()) {
      GoTo(settings_return_ == Page::kDone && have_game ? Page::kDone : Page::kSetup);
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit")) {
      if (env_.on_quit) {
        env_.on_quit();
      }
    }
  }
}

void LauncherDialog::DrawUpdateCheck() {
  const bool pending = update_future_.valid();
  if (pending && update_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    update_result_ = update_future_.get();
    update_answered_ = true;
    REXLOG_INFO("Launcher: update check: {}", update_result_.message);
  }
  ImGui::BeginDisabled(update_future_.valid());
  if (ImGui::Button(update_future_.valid() ? "Checking..." : "Check for updates")) {
    update_answered_ = false;
    update_future_ = CheckForUpdates(env_.version);  // the only network request the port makes, and only here
  }
  ImGui::EndDisabled();
  if (update_answered_) {
    ImGui::SameLine();
    switch (update_result_.status) {
      case UpdateResult::Status::kNewer:
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.6f, 1.0f));
        ImGui::TextUnformatted(update_result_.message.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Open the release page")) {
          if (!OpenInBrowser(update_result_.release_url)) {
            update_result_.message = "Could not open a browser: " + update_result_.release_url;
            update_result_.status = UpdateResult::Status::kError;
          }
        }
        break;
      case UpdateResult::Status::kUpToDate:
        ImGui::TextUnformatted(update_result_.message.c_str());
        break;
      case UpdateResult::Status::kError:
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.45f, 1.0f));
        WrappedText(update_result_.message);
        ImGui::PopStyleColor();
        break;
    }
  } else if (!update_future_.valid()) {
    ImGui::SameLine();
    ImGui::TextDisabled("asks github.com for the latest release; nothing else is sent");
  }
}

void LauncherDialog::UseSource(const std::filesystem::path& source) {
  status_.clear();
  const SourceInfo info = ClassifySource(source);
  switch (info.kind) {
    case SourceKind::kNone:
    case SourceKind::kWrongTitle:
    case SourceKind::kUnsupported:
      status_ = info.note;
      return;
    default:
      break;
  }
  after_work_ = Page::kDone;
  StartExtraction(source);
}

void LauncherDialog::StartExtraction(const std::filesystem::path& source) {
  if (worker_.joinable()) {
    return;
  }
  progress_ = std::make_unique<Progress>();
  worker_done_.store(false);
  ran_extract_ = true;
  ran_shaders_ = false;
  extracted_console_install_ = ClassifySource(source).kind == SourceKind::kConsoleInstall;
  extract_result_ = {};
  shader_result_ = {};
  log_.clear();
  fraction_ = -1.0f;
  work_title_ = "Extracting from " + Utf8(source);
  GoTo(Page::kWorking);
  REXLOG_INFO("Launcher: extracting {} into {}", Utf8(source), Utf8(env_.assets_dir));
  // A package from another build is redone even with the box unticked: it would only cause shader misses.
  const bool shaders = (make_shaders_ || package_stale_) && env_.renut_available;
  stale_redo_started_ = stale_redo_started_ || shaders;
  Progress* progress = progress_.get();
  worker_ = std::thread([this, source, shaders, progress] {
    extract_result_ = ExtractGame(source, env_.assets_dir, env_.dlc_dir, *progress);
    if (extract_result_.status == ExtractResult::Status::kOk && shaders && !progress->cancel.load()) {
      ran_shaders_ = true;
      progress->Log("");
      progress->Log("Preparing the Renut renderer's shaders (vulkan_spirv.bin)...");
      shader_result_ = MakeShaderPackage({env_.exe, env_.exe_dir, env_.assets_dir}, *progress);
    }
    worker_done_.store(true);
  });
}

void LauncherDialog::StartShaders() {
  if (worker_.joinable()) {
    return;
  }
  progress_ = std::make_unique<Progress>();
  worker_done_.store(false);
  ran_extract_ = false;
  ran_shaders_ = true;
  shader_result_ = {};
  log_.clear();
  fraction_ = -1.0f;
  work_title_ = "Preparing the Renut renderer's shaders";
  GoTo(Page::kWorking);
  Progress* progress = progress_.get();
  worker_ = std::thread([this, progress] {
    shader_result_ = MakeShaderPackage({env_.exe, env_.exe_dir, env_.assets_dir}, *progress);
    worker_done_.store(true);
  });
}

void LauncherDialog::PollWorker() {
  if (!worker_.joinable()) {
    return;
  }
  if (progress_) {
    log_ = progress_->Snapshot(fraction_);
  }
  if (!worker_done_.load()) {
    return;
  }
  worker_.join();
  if (ran_extract_) {
    REXLOG_INFO("Launcher: extraction {}: {}",
                extract_result_.status == ExtractResult::Status::kOk ? "done" : "FAILED", extract_result_.message);
  }
  if (ran_shaders_) {
    REXLOG_INFO("Launcher: shaders {}: {}", shader_result_.ok ? "done" : "FAILED", shader_result_.message);
  }
  RefreshStatus();
  if (after_work_ == Page::kSettings) {
    settings_note_ = shader_result_.ok ? shader_result_.message : "Shaders: " + shader_result_.message;
  }
  GoTo(after_work_);
  if (!env_.auto_source.empty() && ran_extract_ && extract_result_.status == ExtractResult::Status::kOk) {
    FinishPlay();
  }
}

void LauncherDialog::PollPicker() {
  std::filesystem::path picked;
  std::string error;
  switch (picker_.Poll(picked, error)) {
    case PickState::kPicked:
      std::snprintf(path_buffer_, sizeof(path_buffer_), "%s", Utf8(picked).c_str());
      UseSource(picked);
      break;
    case PickState::kFailed:
      status_ = error;
      break;
    default:
      break;
  }
}

void LauncherDialog::FinishPlay() {
  if (env_.on_play) {
    auto play = std::move(env_.on_play);
    env_.on_play = nullptr;
    play(ran_extract_ && extract_result_.status == ExtractResult::Status::kOk && extracted_console_install_);
  }
  Close();
}

void LauncherDialog::LoadSettings() {
  renderer_renut_ = env_.renut_available && rex::cvar::Query<std::string>("futari_renderer") == "renut";
  fullscreen_ = rex::cvar::Query<bool>("fullscreen");
  vsync_ = rex::cvar::Query<bool>("vsync");
  resolution_index_ = 0;
  settings_note_.clear();
}

void LauncherDialog::RefreshStatus() {
  int count = 0;
  uint64_t bytes = 0;
  package_ok_ = PackageStatus(env_.exe_dir, count, bytes);
  std::string stamp_detail;
  const StampState stamp = CheckPackageStamp(env_.exe_dir, stamp_detail);
  package_stale_ = StampWantsRedo(stamp);
  if (package_stale_) {
    package_ok_ = false;
    package_status_ = "Shaders were made by a different build, press Redo the shaders";
  } else {
    package_status_ = package_ok_ ? "Renut shaders: ready (" + std::to_string(count) + " shaders, " +
                                        std::to_string(bytes / 1024) + " KB)"
                                  : "Renut shaders: not prepared yet (Renut would draw nothing)";
  }
  dlc_names_.clear();
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(env_.dlc_dir, ec)) {
    if (entry.is_regular_file(ec) && IsXContentFile(entry.path())) {
      dlc_names_.push_back(Utf8(entry.path().filename()) + "  (" +
                           std::to_string(std::filesystem::file_size(entry.path(), ec) / 1024) + " KB)");
    }
  }
  std::sort(dlc_names_.begin(), dlc_names_.end());
}

void LauncherDialog::SaveSettings() {
  rex::cvar::SetFlagByName("fullscreen", fullscreen_ ? "true" : "false");
  rex::cvar::SetFlagByName("vsync", vsync_ ? "true" : "false");
  rex::cvar::SetFlagByName("present_vsync", vsync_ ? "true" : "false");
  if (resolution_index_ > 0 && resolution_index_ <= int(std::size(kResolutions))) {
    const auto& r = kResolutions[resolution_index_ - 1];
    rex::cvar::SetFlagByName("window_width", std::to_string(r.w));
    rex::cvar::SetFlagByName("window_height", std::to_string(r.h));
  }
  if (env_.save_display_settings) {
    env_.save_display_settings();
  }
  std::string error;
  const bool ok = WriteRendererLine(env_.exe_dir / "futari.ini", renderer_renut_ && env_.renut_available, error);
  settings_note_ = ok ? "Saved to futari.ini. The renderer and the window size apply at the next start."
                      : "Could not save: " + error;
  if (ok && renderer_renut_ && !package_ok_) {
    settings_note_ += " Renut has no shaders yet: prepare them above, or the game draws nothing on it.";
  }
  REXLOG_INFO("Launcher: settings saved ({}renderer {})", ok ? "" : "FAILED; ", renderer_renut_ ? "renut" : "default");
}

}  // namespace futari::launcher
