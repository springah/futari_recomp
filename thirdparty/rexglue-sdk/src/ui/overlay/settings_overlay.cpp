/**
 * @file        ui/overlay/settings_overlay.cpp
 *
 * @brief       Settings overlay implementation. See settings_overlay.h for details.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#include <rex/ui/overlay/settings_overlay.h>
#include <rex/cvar.h>
#include <rex/string.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

// Scales the settings overlay's font AND fixed layout dimensions together, so it
// stays readable/navigable on HiDPI displays (the base ImGui font is only 10px).
REXCVAR_DEFINE_DOUBLE(overlay_scale, 1.5, "UI/Window",
                      "Settings overlay UI scale (font + layout); raise on HiDPI displays")
    .range(0.5, 4.0);

// When false (default) the F4 overlay shows only player-facing settings; when
// true it exposes the full cvar registry (internal/diagnostic knobs included).
REXCVAR_DEFINE_BOOL(settings_show_advanced, false, "UI/Window",
                    "Show advanced/internal settings in the F4 overlay");

namespace {

// Tables auto-align the label/value columns at any UI scale, replacing the old
// hand-tuned SameLine offsets (which only worked at scale 1.0).
constexpr ImGuiTableFlags kRowTableFlags =
    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings;

bool IsKeybindCategory(const std::string& cat) {
  return cat == "Input/Keybinds" || (cat.size() > 15 && cat.compare(0, 15, "Input/Keybinds/") == 0);
}

// The curated "simple view" allowlist: a category is player-facing if it equals
// or sits under one of these. Everything else is treated as advanced/internal.
bool IsUserFacingCategory(const std::string& cat) {
  static const char* kAllow[] = {"Audio", "Input", "UI/Window"};
  for (const char* a : kAllow) {
    const std::string prefix(a);
    if (cat == prefix)
      return true;
    if (cat.size() > prefix.size() && cat.compare(0, prefix.size(), prefix) == 0 &&
        cat[prefix.size()] == '/')
      return true;
  }
  return false;
}

// futari_recomp (2026-11-10): the app's hidden cvars (SetSettingsOverlayHiddenCvars).
rex::ui::OverlayCvarFilter g_hidden_filter;

bool IsHiddenByApp(std::string_view name) {
  return g_hidden_filter && g_hidden_filter(name);
}

// futari_recomp (2026-11-11): the app's reset-proof cvars (SetSettingsOverlayResetProofCvars).
rex::ui::OverlayCvarFilter g_reset_proof_filter;

bool KeptOnResetAll(std::string_view name) {
  return IsHiddenByApp(name) || (g_reset_proof_filter && g_reset_proof_filter(name));
}

// Whether a cvar should appear given the current simple/advanced mode. Advanced
// shows everything; simple hides debug-only cvars and non-player categories.
bool IsCvarVisible(const rex::cvar::FlagEntry& entry, bool show_advanced) {
  if (IsHiddenByApp(entry.name))  // futari_recomp (2026-11-10): in both modes
    return false;
  if (show_advanced)
    return true;
  if (entry.is_debug_only)
    return false;
  return IsUserFacingCategory(entry.category);
}

}  // namespace

namespace rex::ui {

SettingsDialog::SettingsDialog(ImGuiDrawer* imgui_drawer, std::filesystem::path config_path,
                               std::function<void()> on_save)
    : ImGuiDialog(imgui_drawer),
      config_path_(std::move(config_path)),
      on_save_(std::move(on_save)) {}

SettingsDialog::~SettingsDialog() {}

static ImVec4 LifecycleColor(rex::cvar::Lifecycle lc) {
  switch (lc) {
    case rex::cvar::Lifecycle::kHotReload:
      return {0.4f, 1.0f, 0.4f, 1.0f};
    case rex::cvar::Lifecycle::kRequiresRestart:
      return {1.0f, 1.0f, 0.4f, 1.0f};
    case rex::cvar::Lifecycle::kInitOnly:
      return {1.0f, 0.4f, 0.4f, 1.0f};
  }
  return {1.0f, 1.0f, 1.0f, 1.0f};
}

static rex::ui::VirtualKey ImGuiKeyToVirtualKey(ImGuiKey key) {
  using VK = rex::ui::VirtualKey;
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
    return static_cast<VK>(static_cast<uint16_t>(VK::kA) + (key - ImGuiKey_A));
  }
  if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
    return static_cast<VK>(static_cast<uint16_t>(VK::k0) + (key - ImGuiKey_0));
  }
  if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24) {
    return static_cast<VK>(static_cast<uint16_t>(VK::kF1) + (key - ImGuiKey_F1));
  }
  if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) {
    return static_cast<VK>(static_cast<uint16_t>(VK::kNumpad0) + (key - ImGuiKey_Keypad0));
  }
  switch (key) {
    case ImGuiKey_Space:
      return VK::kSpace;
    case ImGuiKey_Enter:
      return VK::kReturn;
    case ImGuiKey_Escape:
      return VK::kEscape;
    case ImGuiKey_Tab:
      return VK::kTab;
    case ImGuiKey_Backspace:
      return VK::kBack;
    case ImGuiKey_Delete:
      return VK::kDelete;
    case ImGuiKey_Insert:
      return VK::kInsert;
    case ImGuiKey_Home:
      return VK::kHome;
    case ImGuiKey_End:
      return VK::kEnd;
    case ImGuiKey_PageUp:
      return VK::kPrior;
    case ImGuiKey_PageDown:
      return VK::kNext;
    case ImGuiKey_LeftArrow:
      return VK::kLeft;
    case ImGuiKey_RightArrow:
      return VK::kRight;
    case ImGuiKey_UpArrow:
      return VK::kUp;
    case ImGuiKey_DownArrow:
      return VK::kDown;
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift:
      return VK::kShift;
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl:
      return VK::kControl;
    case ImGuiKey_LeftAlt:
    case ImGuiKey_RightAlt:
      return VK::kMenu;
    case ImGuiKey_CapsLock:
      return VK::kCapital;
    case ImGuiKey_NumLock:
      return VK::kNumLock;
    case ImGuiKey_ScrollLock:
      return VK::kScroll;
    case ImGuiKey_PrintScreen:
      return VK::kSnapshot;
    case ImGuiKey_Pause:
      return VK::kPause;
    case ImGuiKey_GraveAccent:
      return VK::kOem3;
    case ImGuiKey_Minus:
      return VK::kOemMinus;
    case ImGuiKey_Equal:
      return VK::kOemPlus;
    case ImGuiKey_LeftBracket:
      return VK::kOem4;
    case ImGuiKey_RightBracket:
      return VK::kOem6;
    case ImGuiKey_Backslash:
      return VK::kOem5;
    case ImGuiKey_Semicolon:
      return VK::kOem1;
    case ImGuiKey_Apostrophe:
      return VK::kOem7;
    case ImGuiKey_Comma:
      return VK::kOemComma;
    case ImGuiKey_Period:
      return VK::kOemPeriod;
    case ImGuiKey_Slash:
      return VK::kOem2;
    case ImGuiKey_KeypadDecimal:
      return VK::kDecimal;
    case ImGuiKey_KeypadDivide:
      return VK::kDivide;
    case ImGuiKey_KeypadMultiply:
      return VK::kMultiply;
    case ImGuiKey_KeypadSubtract:
      return VK::kSubtract;
    case ImGuiKey_KeypadAdd:
      return VK::kAdd;
    case ImGuiKey_KeypadEnter:
      return VK::kReturn;
    default:
      return VK::kNone;
  }
}

void SettingsDialog::DrawDisplaySection() {
  ImGui::TextDisabled("Display");
  if (!ImGui::BeginTable("##display", 2, kRowTableFlags))
    return;
  ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.45f);
  ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.55f);

  // Resolution preset: sets the shared `resolution` cvar (guest video mode +
  // startup window size). Applies on next launch.
  // futari_recomp (2026-11-10): not drawn when the app hides `resolution`.
  if (!IsHiddenByApp("resolution")) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Resolution");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Render resolution and window size\n[Requires restart to take effect]");
    ImGui::TableNextColumn();
    static const char* kResolutionPresets[] = {"(default)", "1280x720",  "1600x900",
                                               "1920x1080", "2560x1440", "3840x2160"};
    std::string current_res = rex::cvar::Query<std::string>("resolution");
    const char* preview = current_res.empty() ? kResolutionPresets[0] : current_res.c_str();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##res", preview)) {
      bool current_is_preset = current_res.empty();
      for (int i = 0; i < static_cast<int>(std::size(kResolutionPresets)); ++i) {
        bool selected = (i == 0) ? current_res.empty() : (current_res == kResolutionPresets[i]);
        current_is_preset = current_is_preset || selected;
        if (ImGui::Selectable(kResolutionPresets[i], selected))
          rex::cvar::SetFlagByName("resolution", (i == 0) ? "" : kResolutionPresets[i]);
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      // A hand-edited custom value still shows as the (selected) current entry.
      if (!current_is_preset)
        ImGui::Selectable(current_res.c_str(), true);
      ImGui::EndCombo();
    }
  }

  // Fullscreen: applied to the window immediately, and mirrored into the
  // `fullscreen` cvar so Save-to-config persists the current state.
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Fullscreen");
  ImGui::TableNextColumn();
  {
    Window* window = imgui_drawer()->window();
    bool fullscreen = window ? window->IsFullscreen() : rex::cvar::Query<bool>("fullscreen");
    if (ImGui::Checkbox("##fullscreen", &fullscreen)) {
      if (window) {
        // This draw callback runs inside the presenter's paint; resizing the
        // surface mid-frame crashes the driver. Defer to the UI event loop.
        window->app_context().CallInUIThreadDeferred(
            [window, fullscreen] { window->SetFullscreen(fullscreen); });
      }
      rex::cvar::SetFlagByName("fullscreen", fullscreen ? "true" : "false");
    }
    std::string bind = rex::cvar::Query<std::string>("bind_fullscreen");
    if (!bind.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("(%s)", bind.c_str());
    }
  }

  // VSync: one checkbox for both halves. `vsync` paces the guest vblank (read
  // every frame by the GPU vsync worker); `present_vsync` makes the host wait
  // for the display's vertical blank instead of presenting immediately with
  // tearing. Both apply immediately. Players on variable refresh rate displays
  // can turn only `present_vsync` back off in the cvar list below.
  // futari_recomp (2026-11-07): `vsync` is registered by the GPU plugin; an app
  // running its own renderer (no plugin) has no such flag, and a box over a
  // missing flag read as off and set nothing. Then the row is left out: that
  // renderer's own vsync cvar is in the list below.
  if (!rex::cvar::GetFlagByName("vsync").empty()) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("VSync");
    ImGui::TableNextColumn();
    bool vsync = rex::cvar::Query<bool>("vsync");
    if (ImGui::Checkbox("##vsync", &vsync)) {
      rex::cvar::SetFlagByName("vsync", vsync ? "true" : "false");
      rex::cvar::SetFlagByName("present_vsync", vsync ? "true" : "false");
    }
  }

  ImGui::EndTable();
  ImGui::Spacing();
}

void SettingsDialog::DrawCvarRow(const rex::cvar::FlagEntry& entry) {
  const bool read_only = (entry.lifecycle == rex::cvar::Lifecycle::kInitOnly);
  const bool is_command = (entry.type == rex::cvar::FlagType::Command);
  const bool modified = !is_command && rex::cvar::HasNonDefaultValue(entry.name);

  ImGui::PushID(entry.name.c_str());
  ImGui::TableNextRow();

  // Column 0: label (lifecycle-colored, with a marker when changed from default).
  ImGui::TableNextColumn();
  if (read_only)
    ImGui::BeginDisabled();
  ImGui::AlignTextToFramePadding();
  if (modified) {
    ImGui::TextColored(ImVec4(0.35f, 0.7f, 1.0f, 1.0f), "*");
    ImGui::SameLine(0.0f, 4.0f);
  }
  ImGui::TextColored(LifecycleColor(entry.lifecycle), "%s", entry.name.c_str());
  if (ImGui::IsItemHovered()) {
    const char* lifecycle_label = "";
    switch (entry.lifecycle) {
      case rex::cvar::Lifecycle::kHotReload:
        lifecycle_label = "Live - changes apply immediately";
        break;
      case rex::cvar::Lifecycle::kRequiresRestart:
        lifecycle_label = "Requires restart to take effect";
        break;
      case rex::cvar::Lifecycle::kInitOnly:
        lifecycle_label = "Read-only - set at initialization only";
        break;
    }
    if (!entry.description.empty())
      ImGui::SetTooltip("%s\n[%s]", entry.description.c_str(), lifecycle_label);
    else
      ImGui::SetTooltip("[%s]", lifecycle_label);
  }

  // Column 1: value widget, leaving room for a Reset button when modified.
  ImGui::TableNextColumn();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float reset_w =
      ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
  const float control_w = modified ? -reset_w : -FLT_MIN;
  std::string current_val = entry.getter();

  switch (entry.type) {
    case rex::cvar::FlagType::Boolean: {
      bool v = (current_val == "true");
      if (ImGui::Checkbox("##v", &v))
        rex::cvar::SetFlagByName(entry.name, v ? "true" : "false");
      break;
    }
    case rex::cvar::FlagType::String: {
      if (!entry.constraints.allowed_values.empty()) {
        const auto& opts = entry.constraints.allowed_values;
        int cur_idx = 0;
        for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
          if (opts[i] == current_val) {
            cur_idx = i;
            break;
          }
        }
        ImGui::SetNextItemWidth(control_w);
        if (ImGui::BeginCombo("##v", opts[cur_idx].c_str())) {
          for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
            bool sel = (i == cur_idx);
            if (ImGui::Selectable(opts[i].c_str(), sel))
              rex::cvar::SetFlagByName(entry.name, opts[i]);
            if (sel)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      } else {
        char buf[256];
        rex::string::copy_truncating(buf, current_val, sizeof(buf));
        ImGui::SetNextItemWidth(control_w);
        if (ImGui::InputText("##v", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
          rex::cvar::SetFlagByName(entry.name, buf);
      }
      break;
    }
    case rex::cvar::FlagType::Int32:
    case rex::cvar::FlagType::Int64:
    case rex::cvar::FlagType::Uint32:
    case rex::cvar::FlagType::Uint64: {
      int v = std::atoi(current_val.c_str());
      int vmin =
          entry.constraints.min.has_value() ? static_cast<int>(*entry.constraints.min) : INT_MIN;
      int vmax =
          entry.constraints.max.has_value() ? static_cast<int>(*entry.constraints.max) : INT_MAX;
      ImGui::SetNextItemWidth(control_w);
      if (ImGui::InputInt("##v", &v)) {
        v = std::clamp(v, vmin, vmax);
        rex::cvar::SetFlagByName(entry.name, std::to_string(v));
      }
      break;
    }
    case rex::cvar::FlagType::Double: {
      double v = std::atof(current_val.c_str());
      ImGui::SetNextItemWidth(control_w);
      if (ImGui::InputDouble("##v", &v, 0.0, 0.0, "%.4f")) {
        if (entry.constraints.min)
          v = std::max(v, *entry.constraints.min);
        if (entry.constraints.max)
          v = std::min(v, *entry.constraints.max);
        rex::cvar::SetFlagByName(entry.name, std::to_string(v));
      }
      break;
    }
    case rex::cvar::FlagType::Command: {
      if (ImGui::Button("Run##v", ImVec2(-FLT_MIN, 0.0f)))
        entry.command_callback("");
      break;
    }
  }

  if (modified) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset"))
      rex::cvar::ResetToDefault(entry.name);
  }

  if (read_only)
    ImGui::EndDisabled();
  ImGui::PopID();
}

void SettingsDialog::DrawKeybindRow(const rex::cvar::FlagEntry& entry) {
  auto& registry = rex::cvar::GetRegistry();
  // Greyed out for controller keybinds when MnK mode is disabled.
  const bool mnk_disabled =
      (entry.category == "Input/Keybinds/Controller" && !REXCVAR_QUERY(bool, mnk_mode));
  const bool read_only = (entry.lifecycle == rex::cvar::Lifecycle::kInitOnly);
  const bool modified = rex::cvar::HasNonDefaultValue(entry.name);

  ImGui::PushID(entry.name.c_str());
  ImGui::TableNextRow();

  // Column 0: the human label (e.g. "A button"), not the raw cvar name.
  ImGui::TableNextColumn();
  if (read_only)
    ImGui::BeginDisabled();
  if (mnk_disabled)
    ImGui::BeginDisabled();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(entry.description.empty() ? entry.name.c_str()
                                                   : entry.description.c_str());

  // Column 1: current binding + rebind/reset, or the live capture prompt.
  ImGui::TableNextColumn();
  const std::string current_val = entry.getter();
  const bool is_capturing = (capturing_bind_name_ == entry.name);

  // Refuse keys already bound to another action so two actions can never share a
  // key via capture. The rejected key's owner is reported and capture stays active.
  auto conflicting_bind = [&](const std::string& key_name) -> std::string {
    for (auto& other : registry) {
      if (IsKeybindCategory(other.category) && other.name != entry.name &&
          other.getter() == key_name) {
        return other.description.empty() ? other.name : other.description;
      }
    }
    return {};
  };

  if (is_capturing) {
    ImGui::Button("Press any key...");
    auto try_assign = [&](const std::string& key_name) {
      std::string owner = conflicting_bind(key_name);
      if (!owner.empty()) {
        capture_conflict_ = "'" + key_name + "' is bound to " + owner;
        return;
      }
      rex::cvar::SetFlagByName(entry.name, key_name);
      capturing_bind_name_.clear();
      capture_conflict_.clear();
    };

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      capturing_bind_name_.clear();
      capture_conflict_.clear();
    } else {
      for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        auto imgui_key = static_cast<ImGuiKey>(k);
        if (imgui_key == ImGuiKey_Escape)
          continue;
        if (ImGui::IsKeyPressed(imgui_key)) {
          auto vk = ImGuiKeyToVirtualKey(imgui_key);
          std::string name = rex::ui::VirtualKeyToString(vk);
          if (!name.empty())
            try_assign(name);
          else {
            // Unmappable key: cancel capture.
            capturing_bind_name_.clear();
            capture_conflict_.clear();
          }
          break;
        }
      }
      for (int mb = 0; mb < 3; ++mb) {
        if (ImGui::IsMouseClicked(mb)) {
          const char* names[] = {"LMB", "RMB", "MMB"};
          try_assign(names[mb]);
          break;
        }
      }
    }

    // Conflict message on its own line within the value cell.
    if (!capture_conflict_.empty() && capturing_bind_name_ == entry.name)
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", capture_conflict_.c_str());
  } else {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(current_val.empty() ? "(unbound)" : current_val.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Rebind")) {
      capturing_bind_name_ = entry.name;
      capture_conflict_.clear();
    }
    if (modified) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Reset"))
        rex::cvar::SetFlagByName(entry.name, entry.default_value);
    }

    if (!current_val.empty()) {
      int conflict_count = 0;
      for (auto& other : registry) {
        if (IsKeybindCategory(other.category) && other.name != entry.name &&
            other.getter() == current_val)
          conflict_count++;
      }
      if (conflict_count > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(!)");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Key '%s' is also bound to %d other action(s)", current_val.c_str(),
                            conflict_count);
      }
    }
  }

  if (mnk_disabled)
    ImGui::EndDisabled();
  if (read_only)
    ImGui::EndDisabled();
  ImGui::PopID();
}

namespace {
OverlayFontPicker g_font_picker;
}  // namespace

void SetSettingsOverlayFontPicker(OverlayFontPicker picker) { g_font_picker = std::move(picker); }

void SetSettingsOverlayHiddenCvars(OverlayCvarFilter filter) { g_hidden_filter = std::move(filter); }

void SetSettingsOverlayResetProofCvars(OverlayCvarFilter filter) {
  g_reset_proof_filter = std::move(filter);
}

void SettingsDialog::OnDraw(ImGuiIO& io) {
  auto& registry = rex::cvar::GetRegistry();
  const bool show_advanced = REXCVAR_QUERY(bool, settings_show_advanced);

  // Collect the categories that have at least one visible cvar in this mode.
  std::set<std::string> category_set;
  for (auto& entry : registry) {
    if (entry.name == "settings_show_advanced")
      continue;  // surfaced as the header checkbox, not a list row
    if (IsCvarVisible(entry, show_advanced))
      category_set.insert(entry.category);
  }

  // Build the category tree: for "Input/Keybinds/Controller", also register the
  // parent nodes "Input" and "Input/Keybinds".
  struct CatNode {
    std::string full_path;
    std::string label;  // leaf segment (e.g. "Controller")
    std::map<std::string, CatNode> children;
    bool has_direct_entries = false;
  };
  std::map<std::string, CatNode> tree;
  for (auto& cat : category_set) {
    std::map<std::string, CatNode>* level = &tree;
    std::string path_so_far;
    size_t start = 0;
    while (start < cat.size()) {
      size_t slash = cat.find('/', start);
      std::string segment =
          (slash == std::string::npos) ? cat.substr(start) : cat.substr(start, slash - start);
      if (!path_so_far.empty())
        path_so_far += "/";
      path_so_far += segment;
      auto& node = (*level)[segment];
      node.full_path = path_so_far;
      node.label = segment;
      if (path_so_far == cat)
        node.has_direct_entries = true;
      level = &node.children;
      start = (slash == std::string::npos) ? cat.size() : slash + 1;
    }
  }

  const std::string search(search_buf_);
  const bool searching = !search.empty();
  // futari_recomp (2026-11-07): with an app font (SetSettingsOverlayFontPicker) the layout unit follows the font's
  // size (what overlay_scale is for the 10 px base font), so the window, panel and indents keep their proportions.
  ImFont* font = g_font_picker ? g_font_picker(io.DisplaySize.y) : nullptr;
  const float scale = font ? font->LegacySize / 10.0f : static_cast<float>(REXCVAR_GET(overlay_scale));
  if (font)
    ImGui::PushFont(font, font->LegacySize);

  // Open at the scaled size, but never larger than the host viewport, and clamp
  // every frame so the window + resize grips stay on-screen at any overlay_scale.
  const ImVec2 work_size = ImGui::GetMainViewport()->WorkSize;
  ImVec2 want(620.0f * scale, 480.0f * scale);
  want.x = std::min(want.x, work_size.x * 0.95f);
  want.y = std::min(want.y, work_size.y * 0.95f);
  ImGui::SetNextWindowSize(want, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(std::min(320.0f * scale, work_size.x), std::min(240.0f * scale, work_size.y)),
      work_size);
  ImGui::SetNextWindowBgAlpha(0.85f);
  if (!ImGui::Begin("Settings##rex", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    if (font)
      ImGui::PopFont();
    return;
  }
  if (!font)
    ImGui::SetWindowFontScale(scale);

  // Header: search box (full width) + the simple/advanced toggle.
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("##search", search_buf_, sizeof(search_buf_));
  bool adv = show_advanced;
  if (ImGui::Checkbox("Show advanced settings", &adv))
    rex::cvar::SetFlagByName("settings_show_advanced", adv ? "true" : "false");
  if (!show_advanced) {
    ImGui::SameLine();
    ImGui::TextDisabled("(player settings only)");
  }
  ImGui::Separator();

  // Reserve space for the bottom bar (separator + button row + optional hint).
  const std::vector<std::string> pending = rex::cvar::GetPendingRestartFlags();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float footer = style.ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() +
                       (pending.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing());

  // Left: category tree (disabled while searching, since search spans all cats).
  const float panel_width = 160.0f * scale;
  ImGui::BeginChild("##cats", ImVec2(panel_width, -footer), true);
  if (searching)
    ImGui::BeginDisabled();
  std::function<void(const std::map<std::string, CatNode>&, int)> draw_tree;
  draw_tree = [&](const std::map<std::string, CatNode>& nodes, int depth) {
    for (auto& [key, node] : nodes) {
      if (node.children.empty()) {
        bool selected = (selected_category_ == node.full_path);
        if (depth > 0)
          ImGui::Indent(8.0f * scale);
        if (ImGui::Selectable(node.label.c_str(), selected))
          selected_category_ = node.full_path;
        if (depth > 0)
          ImGui::Unindent(8.0f * scale);
      } else {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
        if (node.has_direct_entries && selected_category_ == node.full_path)
          flags |= ImGuiTreeNodeFlags_Selected;
        bool open = ImGui::TreeNodeEx(node.label.c_str(), flags);
        if (ImGui::IsItemClicked() && node.has_direct_entries)
          selected_category_ = node.full_path;
        if (open) {
          draw_tree(node.children, depth + 1);
          ImGui::TreePop();
        }
      }
    }
  };
  // Root node named after the config file.
  std::string root_label = config_path_.stem().string();
  ImGuiTreeNodeFlags root_flags = ImGuiTreeNodeFlags_DefaultOpen;
  if (selected_category_.empty())
    root_flags |= ImGuiTreeNodeFlags_Selected;
  bool root_open = ImGui::TreeNodeEx(root_label.c_str(), root_flags);
  if (ImGui::IsItemClicked())
    selected_category_.clear();
  if (root_open) {
    draw_tree(tree, 1);
    ImGui::TreePop();
  }
  if (searching)
    ImGui::EndDisabled();
  ImGui::EndChild();

  ImGui::SameLine();

  // Exact-or-prefix match against the selected category (root = show all).
  auto category_matches = [&](const std::string& cat) -> bool {
    if (selected_category_.empty())
      return true;
    if (cat == selected_category_)
      return true;
    if (cat.size() > selected_category_.size() &&
        cat.compare(0, selected_category_.size(), selected_category_) == 0 &&
        cat[selected_category_.size()] == '/') {
      return true;
    }
    return false;
  };
  // Case-insensitive substring match against name or description.
  auto matches_search = [&](const rex::cvar::FlagEntry& e) -> bool {
    auto lower = [](std::string s) {
      for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    };
    const std::string q = lower(search);
    return lower(e.name).find(q) != std::string::npos ||
           lower(e.description).find(q) != std::string::npos;
  };

  // Right: the settings list (Display quick-section at root, then the table).
  ImGui::BeginChild("##cvars", ImVec2(0, -footer), false);
  if (!searching && selected_category_.empty())
    DrawDisplaySection();

  if (ImGui::BeginTable("##cvar_table", 2, kRowTableFlags)) {
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.45f);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
    for (auto& entry : registry) {
      if (entry.name == "settings_show_advanced")
        continue;
      if (!IsCvarVisible(entry, show_advanced))
        continue;
      if (!searching) {
        if (!category_matches(entry.category))
          continue;
      } else if (!matches_search(entry)) {
        continue;
      }
      if (IsKeybindCategory(entry.category))
        DrawKeybindRow(entry);
      else
        DrawCvarRow(entry);
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();

  // Bottom bar: Save / Reset-all, the config filename, and a restart hint.
  ImGui::Separator();
  if (ImGui::Button("Save to config")) {
    if (on_save_)
      on_save_();
    else
      rex::cvar::SaveConfig(config_path_);
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset all to defaults")) {
    // futari_recomp (2026-11-10): the app's hidden cvars keep their values (the player can't see them reset);
    // (2026-11-11) so do its reset-proof ones.
    if (g_hidden_filter || g_reset_proof_filter) {
      for (auto& entry : registry) {
        if (!KeptOnResetAll(entry.name))
          rex::cvar::ResetToDefault(entry.name);
      }
    } else {
      rex::cvar::ResetAllToDefaults();
    }
  }
  if (ImGui::IsItemHovered()) {
    // futari_recomp (2026-11-10): app-hidden cvars are not reset; (2026-11-11) nor the app's reset-proof ones.
    ImGui::SetTooltip(g_reset_proof_filter || g_hidden_filter
                          ? "Reset the settings to their defaults, except the ones the game keeps (cannot be undone)"
                          : "Reset every setting to its default (cannot be undone)");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%s)", config_path_.filename().string().c_str());
  if (!pending.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                       "%zu change(s) need a restart to take effect", pending.size());
  }

  ImGui::End();
  if (font)
    ImGui::PopFont();
}

}  // namespace rex::ui
