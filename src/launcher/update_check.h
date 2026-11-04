/**
 * @file        update_check.h
 * @brief       "Check for updates" on the launcher's Settings page: one HTTPS GET of the latest GitHub release.
 *
 * futari_recomp (BSD 3-Clause), 2026-11-04. Never automatic: nothing touches the network unless the player presses
 * the button. The request is GET https://api.github.com/repos/springah/futari_recomp/releases/latest and it sends
 * nothing about the player or the machine: no cookie, no token, only a User-Agent of "futari_recomp/<version>" (the
 * GitHub API refuses requests without one) and an Accept header. Windows: WinHTTP. macOS: the system libcurl. Other
 * platforms: "not available in this build". The answer's tag_name is found by a small scan, no JSON library.
 */
#pragma once

#include <future>
#include <string>

namespace futari::launcher {

struct UpdateResult {
  enum class Status { kUpToDate, kNewer, kError };
  Status status = Status::kError;
  std::string latest_tag;    // "v1.5.6"
  std::string release_url;   // https://github.com/springah/futari_recomp/releases/tag/<tag>
  std::string message;       // the line the Settings page shows
};

// -1, 0, 1 as `a` is older than, the same as, newer than `b`. "v1.5.6", "1.5.6", "1.5.5-rc3": numeric
// major.minor.patch first, then a release beats any pre-release of the same number (-rc3 < release), rcN by N.
int CompareVersions(const std::string& a, const std::string& b);

// The tag_name string of a GitHub release JSON, or "" if there is none.
std::string FindTagName(const std::string& json);

// Starts the request on a thread of its own and returns at once; the future becomes ready when it is answered
// (10 s connect / 15 s total at most). `current` is this build's FUTARI_VERSION_STRING.
std::future<UpdateResult> CheckForUpdates(const std::string& current);

// Opens `url` in the default browser (ShellExecute / `open`). Only https://github.com/ URLs are opened.
bool OpenInBrowser(const std::string& url);

}  // namespace futari::launcher
