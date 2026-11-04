/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#ifndef REX_UI_IMGUI_DRAWER_H_
#define REX_UI_IMGUI_DRAWER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <rex/ui/immediate_drawer.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

struct ImDrawData;
struct ImFontAtlas;
struct ImGuiContext;
struct ImGuiIO;
enum ImGuiKey : int;

namespace rex {
namespace ui {

class ImGuiDialog;
class Window;

class ImGuiDrawer : public WindowInputListener, public UIDrawer {
 public:
  using FontSetupCallback = std::function<void(ImFontAtlas*)>;
  ImGuiDrawer(Window* window, size_t z_order, FontSetupCallback font_setup = nullptr);
  ~ImGuiDrawer();

  ImGuiIO& GetIO();
  bool HasDialogs() const { return !dialogs_.empty(); }
  // A dialog that wants gamepad navigation is up (ImGuiDialog::WantsGamepadNavigation): it owns the input.
  // Safe from any thread (the input drivers ask from guest threads): a flag the UI thread keeps.
  bool HasNavigationDialog() const { return navigation_dialog_up_.load(std::memory_order_relaxed); }

  Window* window() const { return window_; }

  void AddDialog(ImGuiDialog* dialog);
  void RemoveDialog(ImGuiDialog* dialog);

  // SetPresenter may be called from the destructor.
  void SetPresenter(Presenter* new_presenter);
  void SetImmediateDrawer(ImmediateDrawer* new_immediate_drawer);
  void SetPresenterAndImmediateDrawer(Presenter* new_presenter,
                                      ImmediateDrawer* new_immediate_drawer) {
    SetPresenter(new_presenter);
    SetImmediateDrawer(new_immediate_drawer);
  }

  void Draw(UIDrawContext& ui_draw_context) override;

 protected:
  void OnKeyDown(KeyEvent& e) override;
  void OnKeyUp(KeyEvent& e) override;
  void OnKeyChar(KeyEvent& e) override;
  void OnMouseDown(MouseEvent& e) override;
  void OnMouseMove(MouseEvent& e) override;
  void OnMouseUp(MouseEvent& e) override;
  void OnMouseWheel(MouseEvent& e) override;
  void OnTouchEvent(TouchEvent& e) override;
  // For now, no need for OnDpiChanged because redrawing is done continuously.

 private:
  void Initialize();

  void SetupFontTexture();

  void RenderDrawLists(ImDrawData* data, UIDrawContext& ui_draw_context);

  void ClearInput();
  void OnKey(KeyEvent& e, bool is_down);
  void UpdateMousePosition(float x, float y);
  void SwitchToPhysicalMouseAndUpdateMousePosition(const MouseEvent& e);

  bool IsDrawingDialogs() const { return dialog_loop_next_index_ != SIZE_MAX; }
  void DetachIfLastDialogRemoved();

  std::optional<ImGuiKey> VirtualKeyToImGuiKey(VirtualKey vkey);

  // Gamepad navigation (futari_recomp, 2026-11-04): while a dialog wants it, SDL3's gamepad subsystem is held open
  // here and every pad's buttons and left stick are fed to ImGui as ImGuiKey_Gamepad* events, once per frame.
  // Released (the subsystem's reference dropped) as soon as no such dialog remains, so a later SDL input driver
  // sees its own GAMEPAD_ADDED events. A button held when navigation starts is ignored until it is released.
  void UpdateGamepadNavigation();
  void ReleaseGamepadNavigation();

  Window* window_;
  size_t z_order_;
  FontSetupCallback font_setup_;

  ImGuiContext* internal_state_ = nullptr;

  // All currently-attached dialogs that get drawn.
  std::vector<ImGuiDialog*> dialogs_;
  // Using an index, not an iterator, because after the erasure, the adjustment
  // must be done for the vector element indices that would be in the iterator
  // range that would be invalidated.
  // SIZE_MAX if not currently in the dialog loop.
  size_t dialog_loop_next_index_ = SIZE_MAX;

  Presenter* presenter_ = nullptr;

  ImmediateDrawer* immediate_drawer_ = nullptr;
  // Resources specific to an immediate drawer - must be destroyed before
  // detaching the presenter.
  std::unique_ptr<ImmediateTexture> font_texture_;

  // If there's an active pointer, the ImGui mouse is controlled by this touch.
  // If it's TouchEvent::kPointerIDNone, the ImGui mouse is controlled by the
  // mouse.
  uint32_t touch_pointer_id_ = TouchEvent::kPointerIDNone;
  // Whether after the next frame (since the mouse up event needs to be handled
  // with the correct mouse position still), the ImGui mouse position should be
  // reset (for instance, after releasing a touch), so it's not hovering over
  // anything.
  bool reset_mouse_position_after_next_frame_ = false;

  double frame_time_tick_frequency_;
  uint64_t last_frame_time_ticks_;

  std::atomic<bool> navigation_dialog_up_{false};
  bool gamepad_nav_active_ = false;
  bool gamepad_subsystem_held_ = false;
  std::vector<void*> nav_gamepads_;  // SDL_Gamepad*
  uint64_t nav_keys_down_ = 0;       // bit per entry of the drawer's gamepad key table, as last fed to ImGui
  uint64_t nav_keys_armed_ = 0;      // seen released since navigation started
};

}  // namespace ui
}  // namespace rex

#endif  // REX_UI_IMGUI_DRAWER_H_
