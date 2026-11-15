// Standalone test of src/renut_engine/gpu/futari_screen_rules.h (pure std): which of Futari's targets hold the
// gameplay image (kept at 1x), which resolve destinations hold it, and when a draw is the gameplay blit that Sharp
// bilinear or On redraws with a host shader. Locally with any C++20 compiler:
//
//   clang++ -std=c++20 -Wall -Wextra -I src tests/futari_screen_test.cpp -o futari_screen_test
#include "renut_engine/gpu/futari_screen_rules.h"

#include <cstdint>
#include <cstdio>
#include <optional>

namespace {

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n", what);
}

using renut::gpu::futari_screen::BlitHostPixelShader;
using renut::gpu::futari_screen::BlitSmoothing;
using renut::gpu::futari_screen::BlitSmoothingOf;
using renut::gpu::futari_screen::GameplayResolvePages;
using renut::gpu::futari_screen::HostPixelShaderName;
using renut::gpu::futari_screen::IsGameplayBlit;
using renut::gpu::futari_screen::IsGameplayTarget;
using renut::gpu::futari_screen::kBlitPixelShaderHash;
using renut::gpu::futari_screen::kGameplayResolvePages;
using renut::gpu::futari_screen::kHostPixelShaderBilinear;
using renut::gpu::futari_screen::kHostPixelShaderGuest;
using renut::gpu::futari_screen::kHostPixelShaderSharp;
using renut::gpu::futari_screen::LastBlitSource;

constexpr uint32_t kArgb = 0x1A2201BF;  // the 320x240 and 640x480 targets' format
constexpr uint32_t kXrgb = 0x28280186;  // the 640x512 target's
constexpr uint32_t kBackBuffer = 0x18280186;

void GameplayTargets() {
  Check(IsGameplayTarget(320, 240, kArgb, 1808), "Arcade 320x240 at base 1808");
  Check(IsGameplayTarget(640, 480, kArgb, 1808), "Xbox 360 640x480 at base 1808");
  Check(IsGameplayTarget(640, 512, kXrgb, 720), "Xbox 360 640x512 at the 1280x720 boot base");
  Check(IsGameplayTarget(640, 512, kXrgb, 1632), "Xbox 360 640x512 at the 1920x1080 boot base");
  Check(IsGameplayTarget(640, 512, kXrgb, 0), "640x512 at any base (an over-EDRAM boot frame)");
}

void NotGameplayTargets() {
  Check(!IsGameplayTarget(80, 60, kArgb, 1808), "the 80x60 downsample shares format and base, not the size");
  Check(!IsGameplayTarget(320, 240, kArgb, 1807), "320x240 at another base");
  Check(!IsGameplayTarget(640, 480, kArgb, 0), "640x480 at another base");
  Check(!IsGameplayTarget(320, 240, kXrgb, 1808), "320x240 in another format");
  Check(!IsGameplayTarget(640, 512, kArgb, 1808), "640x512 in the other format");
  Check(!IsGameplayTarget(1920, 1080, kBackBuffer, 0), "the back buffer");
  Check(!IsGameplayTarget(1280, 720, kBackBuffer, 0), "a 1280x720 back buffer");
  Check(!IsGameplayTarget(240, 320, kArgb, 1808), "320x240 turned on its side");
}

void PagesRecordedAndRemoved() {
  GameplayResolvePages pages;
  Check(!pages.Contains(0x123), "empty set holds nothing");
  pages.Note(true, 0x123);
  Check(pages.Contains(0x123), "a gameplay resolve records its destination page");
  pages.Note(true, 0x123);
  pages.Note(false, 0x123);
  Check(!pages.Contains(0x123), "another source resolving into the page removes it (once recorded twice)");
  pages.Note(false, 0x456);
  Check(!pages.Contains(0x456), "a non-gameplay resolve never records");
  pages.Note(true, 0x1);
  pages.Note(true, 0x2);
  pages.Note(false, 0x1);
  Check(!pages.Contains(0x1) && pages.Contains(0x2), "removal touches only its own page");
}

void PagesBounded() {
  GameplayResolvePages pages;
  for (uint32_t p = 1; p <= kGameplayResolvePages + 3; ++p)
    pages.Note(true, p);
  Check(!pages.Contains(1) && !pages.Contains(3), "past the bound the oldest pages go first");
  Check(pages.Contains(4) && pages.Contains(kGameplayResolvePages + 3), "the newest pages stay");
  pages.Note(true, 4);
  pages.Note(true, kGameplayResolvePages + 4);
  Check(pages.Contains(4), "a page noted again is the newest again");
}

void SmoothingFromTheGamesWord() {
  Check(BlitSmoothingOf(0) == BlitSmoothing::kGame, "Off: the game's shader");
  Check(BlitSmoothingOf(1) == BlitSmoothing::kBilinear, "On: float bilinear");
  Check(BlitSmoothingOf(4) == BlitSmoothing::kSharp, "Sharp bilinear");
  Check(BlitSmoothingOf(2) == BlitSmoothing::kGame, "the game's mode 2: its own shader");
  Check(BlitSmoothingOf(3) == BlitSmoothing::kGame, "the game's mode 3: its own shader");
  Check(BlitSmoothingOf(5) == BlitSmoothing::kGame, "an unknown value: the game's shader");
}

void HostShaderForTheBlit() {
  constexpr uint64_t blit = kBlitPixelShaderHash;
  Check(BlitHostPixelShader(BlitSmoothing::kSharp, blit, true, false) == kHostPixelShaderSharp,
        "Sharp, blit PS, gameplay source, frame target: sharp_bilinear_ps");
  Check(BlitHostPixelShader(BlitSmoothing::kBilinear, blit, true, false) == kHostPixelShaderBilinear,
        "On, the same blit: the k = 1 shader");
  Check(BlitHostPixelShader(BlitSmoothing::kGame, blit, true, false) == kHostPixelShaderGuest, "Off: the guest PS");
  Check(kHostPixelShaderGuest == 0 && kHostPixelShaderSharp != kHostPixelShaderBilinear,
        "0 is the guest's; the two host shaders differ");
  for (const BlitSmoothing smoothing : {BlitSmoothing::kSharp, BlitSmoothing::kBilinear}) {
    Check(BlitHostPixelShader(smoothing, 0xE706F351A6A124C5ull, true, false) == 0,
          "the downsample's PS on the same source");
    Check(BlitHostPixelShader(smoothing, blit, false, false) == 0, "the blit PS on another texture");
    Check(BlitHostPixelShader(smoothing, blit, true, true) == 0, "into a gameplay target");
  }
  Check(kBlitPixelShaderHash == 0xB504A970B5672C5Full, "the measured blit PS");
}

// The suffix a pipeline's debug name gets for its host pixel shader.
void HostShaderNames() {
  Check(HostPixelShaderName(kHostPixelShaderGuest).empty(), "the guest's: no suffix");
  Check(HostPixelShaderName(kHostPixelShaderSharp) == " sharp_bilinear_ps", "Sharp: sharp_bilinear_ps");
  Check(HostPixelShaderName(kHostPixelShaderBilinear) == " blit_bilinear_ps", "On: blit_bilinear_ps");
  Check(HostPixelShaderName(7) == " host_ps_7", "any other value: its number");
}

void GameplayBlitUnderEveryMode() {
  Check(IsGameplayBlit(kBlitPixelShaderHash, true, false), "the blit, whatever the Smoothing mode");
  Check(!IsGameplayBlit(0xE706F351A6A124C5ull, true, false), "the downsample's PS on a gameplay source");
  Check(!IsGameplayBlit(kBlitPixelShaderHash, false, false), "the blit PS on another texture");
  Check(!IsGameplayBlit(kBlitPixelShaderHash, true, true), "the blit PS into a gameplay target");
}

void LastBlitSourceOfTheFrame() {
  LastBlitSource last;
  Check(!last.Take(), "no blit yet: no source");
  last.Note(0x123);
  last.Note(0x456);
  const std::optional<uint32_t> page = last.Take();
  Check(page && *page == 0x456, "the frame's last blit's page");
  Check(!last.Take(), "taken once: the next frame starts without a source");
  last.Note(0);
  Check(last.Take() == std::optional<uint32_t>(0), "page 0 is a page");
}

}  // namespace

int main() {
  GameplayTargets();
  NotGameplayTargets();
  PagesRecordedAndRemoved();
  PagesBounded();
  SmoothingFromTheGamesWord();
  HostShaderForTheBlit();
  HostShaderNames();
  GameplayBlitUnderEveryMode();
  LastBlitSourceOfTheFrame();
  std::printf("futari_screen_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
