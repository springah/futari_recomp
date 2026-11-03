/**
 * @file        launcher_dialog.cpp
 * @brief       The launcher's screens (see the header). UI thread throughout; the work runs on worker_.
 */
#include "launcher/launcher_dialog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

namespace futari::launcher {

namespace {

std::string Utf8(const std::filesystem::path& p) { return rex::path_to_utf8(p); }

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

}  // namespace

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
  PollPicker();
  PollWorker();

  const float scale = io.DisplaySize.y >= 1000.0f ? 2.0f : 1.5f;
  const ImVec2 size(std::min(io.DisplaySize.x - 40.0f, 500.0f * scale),
                    std::min(io.DisplaySize.y - 40.0f, 300.0f * scale));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(env_.in_game ? 0.92f : 1.0f);
  const std::string title = "Mushihimesama Futari (recompiled) v" + env_.version + "##futari_launcher";
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
  bool open = true;
  if (ImGui::Begin(title.c_str(), env_.in_game ? &open : nullptr, kFlags)) {
    ImGui::SetWindowFontScale(scale);
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
  if (!open) {
    Close();
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
    page_ = Page::kSettings;
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
  if (ImGui::Button("Cancel")) {
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
  if (have_game && !env_.in_game && env_.on_play) {
    if (ImGui::Button("Play")) {
      FinishPlay();
      return;
    }
    ImGui::SameLine();
  }
  if (ImGui::Button(have_game ? "Settings" : "Back")) {
    if (have_game) {
      LoadSettings();
      RefreshStatus();
      page_ = Page::kSettings;
    } else {
      status_.clear();
      page_ = Page::kSetup;
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
    if (ImGui::RadioButton("Default", !renderer_renut_)) {
      renderer_renut_ = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Renut (Vulkan)", renderer_renut_)) {
      renderer_renut_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("applies at the next start");
    ImGui::TextUnformatted(package_status_.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!have_game || worker_.joinable());
    if (ImGui::Button(package_ok_ ? "Redo the shaders" : "Prepare the shaders")) {
      after_work_ = Page::kSettings;
      StartShaders();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
  }
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
  if (!settings_note_.empty()) {
    ImGui::Spacing();
    WrappedText(settings_note_);
  }
  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button(env_.in_game ? "Save and close" : "Save")) {
    SaveSettings();
    if (env_.in_game) {
      Close();
      return;
    }
  }
  ImGui::SameLine();
  if (env_.in_game) {
    if (ImGui::Button("Close")) {
      Close();
      return;
    }
  } else {
    if (have_game && env_.on_play) {
      if (ImGui::Button("Play")) {
        FinishPlay();
        return;
      }
      ImGui::SameLine();
    }
    if (ImGui::Button("Setup")) {
      page_ = Page::kSetup;
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit")) {
      if (env_.on_quit) {
        env_.on_quit();
      }
    }
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
  extract_result_ = {};
  shader_result_ = {};
  log_.clear();
  fraction_ = -1.0f;
  work_title_ = "Extracting from " + Utf8(source);
  page_ = Page::kWorking;
  REXLOG_INFO("Launcher: extracting {} into {}", Utf8(source), Utf8(env_.assets_dir));
  const bool shaders = make_shaders_ && env_.renut_available;
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
  page_ = Page::kWorking;
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
  page_ = after_work_;
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
    play();
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
  package_status_ = package_ok_ ? "Renut shaders: ready (" + std::to_string(count) + " shaders, " +
                                      std::to_string(bytes / 1024) + " KB)"
                                : "Renut shaders: not prepared yet (Renut would draw nothing)";
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
