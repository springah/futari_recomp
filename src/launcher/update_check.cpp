/**
 * @file        update_check.cpp
 * @brief       See the header. The request runs on a detached thread that owns a std::promise, so a dialog closed
 *              mid-request never waits for it.
 */
#include "launcher/update_check.h"

#include <cctype>
#include <cstdlib>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#elif defined(__APPLE__)
#include <curl/curl.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace futari::launcher {

namespace {

constexpr const char* kApiHost = "api.github.com";
constexpr const char* kApiPath = "/repos/springah/futari_recomp/releases/latest";
constexpr const char* kReleasePage = "https://github.com/springah/futari_recomp/releases/tag/";

struct Version {
  int part[3] = {0, 0, 0};
  bool prerelease = false;
  int pre_number = 0;
};

Version ParseVersion(const std::string& text) {
  Version v;
  size_t i = 0;
  while (i < text.size() && !std::isdigit(static_cast<unsigned char>(text[i]))) {
    ++i;  // "v", "futari-v"
  }
  for (int k = 0; k < 3 && i < text.size(); ++k) {
    int n = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
      n = n * 10 + (text[i] - '0');
      ++i;
    }
    v.part[k] = n;
    if (i < text.size() && text[i] == '.') {
      ++i;
    } else {
      break;
    }
  }
  if (i < text.size() && text[i] == '-') {
    v.prerelease = true;
    while (i < text.size() && !std::isdigit(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
      v.pre_number = v.pre_number * 10 + (text[i] - '0');
      ++i;
    }
  }
  return v;
}

bool SafeTag(const std::string& tag) {
  if (tag.empty() || tag.size() > 64) {
    return false;
  }
  for (char c : tag) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

// The HTTP GET itself: the body and the status, or false with an error.
bool HttpGet(const std::string& user_agent, std::string& body, long& status, std::string& error);

#ifdef _WIN32
std::wstring Widen(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(size_t(n > 0 ? n - 1 : 0), L'\0');
  if (n > 0) {
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  }
  return w;
}

bool HttpGet(const std::string& user_agent, std::string& body, long& status, std::string& error) {
  struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
      if (h) {
        WinHttpCloseHandle(h);
      }
    }
  };
  Handle session, connect, request;
  // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY (Windows 8.1+) follows the system proxy settings.
  session.h = WinHttpOpen(Widen(user_agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session.h) {
    error = "WinHTTP is unavailable (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 15000);
  connect.h = WinHttpConnect(session.h, Widen(kApiHost).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!connect.h) {
    error = "cannot reach api.github.com (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  request.h = WinHttpOpenRequest(connect.h, L"GET", Widen(kApiPath).c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request.h) {
    error = "cannot open the request (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
  if (!WinHttpSendRequest(request.h, headers, DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    const DWORD e = GetLastError();
    error = e == ERROR_WINHTTP_TIMEOUT ? "no answer from api.github.com (timed out)"
          : e == ERROR_WINHTTP_NAME_NOT_RESOLVED ? "cannot reach api.github.com (offline?)"
                                                 : "the request failed (WinHTTP error " + std::to_string(e) + ")";
    return false;
  }
  DWORD code = 0;
  DWORD code_size = sizeof(code);
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &code, &code_size, WINHTTP_NO_HEADER_INDEX);
  status = long(code);
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.h, &available)) {
      error = "the answer was cut off (WinHTTP error " + std::to_string(GetLastError()) + ")";
      return false;
    }
    if (available == 0) {
      break;
    }
    std::vector<char> chunk(available);
    DWORD read = 0;
    if (!WinHttpReadData(request.h, chunk.data(), available, &read)) {
      error = "the answer was cut off (WinHTTP error " + std::to_string(GetLastError()) + ")";
      return false;
    }
    body.append(chunk.data(), read);
    if (body.size() > (4u << 20)) {
      break;  // a release JSON is a few KB; never hold more than this
    }
  }
  return true;
}
#elif defined(__APPLE__)
size_t AppendBody(char* data, size_t size, size_t count, void* user) {
  auto* body = static_cast<std::string*>(user);
  if (body->size() > (4u << 20)) {
    return 0;  // a release JSON is a few KB; abort anything larger
  }
  body->append(data, size * count);
  return size * count;
}

bool HttpGet(const std::string& user_agent, std::string& body, long& status, std::string& error) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    error = "libcurl is unavailable";
    return false;
  }
  const std::string url = std::string("https://") + kApiHost + kApiPath;
  curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // a worker thread: no SIGALRM for the resolver timeout
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  const CURLcode rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK) {
    error = rc == CURLE_OPERATION_TIMEDOUT        ? "no answer from api.github.com (timed out)"
          : rc == CURLE_COULDNT_RESOLVE_HOST       ? "cannot reach api.github.com (offline?)"
                                                    : std::string("the request failed: ") + curl_easy_strerror(rc);
    return false;
  }
  return true;
}
#else
bool HttpGet(const std::string&, std::string&, long&, std::string& error) {
  error = "update checks are not available in this build; see github.com/springah/futari_recomp/releases";
  return false;
}
#endif

UpdateResult RunCheck(const std::string& current) {
  UpdateResult result;
  std::string body;
  std::string error;
  long status = 0;
  if (!HttpGet("futari_recomp/" + current, body, status, error)) {
    result.message = "Could not check for updates: " + error;
    return result;
  }
  if (status == 404) {
    result.message = "Could not check for updates: no release is published yet";
    return result;
  }
  if (status == 403 || status == 429) {
    result.message = "Could not check for updates: GitHub refused for now (rate limit); try again later";
    return result;
  }
  if (status != 200) {
    result.message = "Could not check for updates: GitHub answered HTTP " + std::to_string(status);
    return result;
  }
  const std::string tag = FindTagName(body);
  if (!SafeTag(tag)) {
    result.message = "Could not check for updates: the answer holds no release tag";
    return result;
  }
  result.latest_tag = tag;
  result.release_url = kReleasePage + tag;
  if (CompareVersions(current, tag) < 0) {
    result.status = UpdateResult::Status::kNewer;
    result.message = tag + " is available (this is v" + current + ")";
  } else {
    result.status = UpdateResult::Status::kUpToDate;
    result.message = "You are up to date (v" + current + "; the latest release is " + tag + ")";
  }
  return result;
}

}  // namespace

int CompareVersions(const std::string& a, const std::string& b) {
  const Version va = ParseVersion(a), vb = ParseVersion(b);
  for (int k = 0; k < 3; ++k) {
    if (va.part[k] != vb.part[k]) {
      return va.part[k] < vb.part[k] ? -1 : 1;
    }
  }
  if (va.prerelease != vb.prerelease) {
    return va.prerelease ? -1 : 1;
  }
  if (va.pre_number != vb.pre_number) {
    return va.pre_number < vb.pre_number ? -1 : 1;
  }
  return 0;
}

std::string FindTagName(const std::string& json) {
  const std::string key = "\"tag_name\"";
  size_t i = json.find(key);
  if (i == std::string::npos) {
    return {};
  }
  i += key.size();
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
    ++i;
  }
  if (i >= json.size() || json[i] != ':') {
    return {};
  }
  ++i;
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
    ++i;
  }
  if (i >= json.size() || json[i] != '"') {
    return {};
  }
  const size_t end = json.find('"', i + 1);
  if (end == std::string::npos) {
    return {};
  }
  return json.substr(i + 1, end - i - 1);  // a tag never holds an escaped quote; SafeTag rejects odd ones
}

std::future<UpdateResult> CheckForUpdates(const std::string& current) {
  std::promise<UpdateResult> promise;
  std::future<UpdateResult> future = promise.get_future();
  std::thread([promise = std::move(promise), current]() mutable { promise.set_value(RunCheck(current)); }).detach();
  return future;
}

bool OpenInBrowser(const std::string& url) {
  if (url.rfind("https://github.com/", 0) != 0) {
    return false;
  }
#ifdef _WIN32
  const auto r = reinterpret_cast<INT_PTR>(
      ShellExecuteW(nullptr, L"open", Widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return r > 32;
#elif defined(__APPLE__)
  std::string open = "/usr/bin/open";
  std::string arg = url;
  char* argv[] = {open.data(), arg.data(), nullptr};
  pid_t pid = 0;
  if (posix_spawn(&pid, open.c_str(), nullptr, nullptr, argv, environ) != 0) {
    return false;
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
  (void)url;
  return false;
#endif
}

}  // namespace futari::launcher
