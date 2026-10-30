// The server list of "Join a session" (server_browser.h). A co-op server answers GET /dccoop/info with its name,
// version and players, a datagram "DCCOOP?" to its UDP port 8080 with "DCCOOP!" and the same, and GET
// /dccoop/servers with the servers listed on it.
#define WIN32_LEAN_AND_MEAN
#include "server_browser.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr int kPort = 8080;
constexpr int kTimeoutMs = 900;

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
  return w;
}

bool IsIPv4(const std::string& a) {
  in_addr x{};
  return inet_pton(AF_INET, a.c_str(), &x) == 1;
}

// A plain HTTP/1.0 GET with a timeout; the body, or nothing.
bool HttpGet(const std::string& address, const std::string& path, std::string* body, int* ms) {
  const auto start = Clock::now();
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  u_long nonblocking = 1;
  ioctlsocket(s, FIONBIO, &nonblocking);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(kPort);
  inet_pton(AF_INET, address.c_str(), &to.sin_addr);
  connect(s, reinterpret_cast<sockaddr*>(&to), sizeof to);
  fd_set w;
  FD_ZERO(&w);
  FD_SET(s, &w);
  timeval tv{0, kTimeoutMs * 1000};
  if (select(0, nullptr, &w, nullptr, &tv) != 1) { closesocket(s); return false; }
  int err = 0, len = sizeof err;
  getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
  if (err) { closesocket(s); return false; }
  const std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + address + "\r\nConnection: close\r\n\r\n";
  send(s, request.data(), int(request.size()), 0);
  std::string reply;
  char buf[4096];
  for (;;) {
    const int left = kTimeoutMs * 2 - int(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    if (left <= 0) break;
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    timeval t{0, left * 1000};
    if (select(0, &r, nullptr, nullptr, &t) != 1) break;
    const int n = recv(s, buf, sizeof buf, 0);
    if (n <= 0) break;
    reply.append(buf, n);
    if (reply.size() > 256 * 1024) break;
  }
  closesocket(s);
  if (ms) *ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
  const size_t head = reply.find("\r\n\r\n");
  if (reply.compare(0, 9, "HTTP/1.1 ") && reply.compare(0, 9, "HTTP/1.0 ")) return false;
  if (head == std::string::npos || reply.compare(9, 3, "200")) return false;
  *body = reply.substr(head + 4);
  return true;
}

// The Tailscale peers that are online (tailscale status --json), when Tailscale is installed.
std::vector<std::string> TailscalePeers() {
  std::vector<std::string> out;
  wchar_t pf[MAX_PATH] = L"";
  if (!GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH)) return out;
  const std::wstring exe = std::wstring(pf) + L"\\Tailscale\\tailscale.exe";
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return out;
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE read = nullptr, write = nullptr;
  if (!CreatePipe(&read, &write, &sa, 0)) return out;
  SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{sizeof si};
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = write;
  si.hStdError = write;
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + exe + L"\" status --json";
  const bool started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(write);
  std::string text;
  if (started) {
    const auto start = Clock::now();
    char buf[8192];
    for (;;) {
      DWORD avail = 0;
      if (!PeekNamedPipe(read, nullptr, 0, nullptr, &avail, nullptr)) break;
      if (avail) {
        DWORD got = 0;
        if (!ReadFile(read, buf, (std::min)(DWORD(sizeof buf), avail), &got, nullptr) || !got) break;
        text.append(buf, got);
        continue;
      }
      if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) {
        DWORD got = 0;
        while (PeekNamedPipe(read, nullptr, 0, nullptr, &avail, nullptr) && avail &&
               ReadFile(read, buf, (std::min)(DWORD(sizeof buf), avail), &got, nullptr) && got)
          text.append(buf, got);
        break;
      }
      if (Clock::now() - start > std::chrono::milliseconds(1500)) { TerminateProcess(pi.hProcess, 1); break; }
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  CloseHandle(read);
  try {
    const Json status = Json::parse(text);
    for (const auto& [key, peer] : status.value("Peer", Json::object()).items()) {
      if (!peer.value("Online", false)) continue;
      for (const auto& ip : peer.value("TailscaleIPs", Json::array()))
        if (ip.is_string() && IsIPv4(ip.get<std::string>())) { out.push_back(ip.get<std::string>()); break; }
    }
  } catch (...) {
  }
  return out;
}

// The directed broadcast address of every IPv4 network of this PC, and the limited one.
std::vector<in_addr> BroadcastAddresses() {
  std::vector<in_addr> out;
  in_addr all{};
  all.s_addr = INADDR_BROADCAST;
  out.push_back(all);
  ULONG size = 0;
  GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, nullptr, &size);
  std::vector<unsigned char> buf(size);
  auto* first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  if (!size || GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, first, &size) != NO_ERROR)
    return out;
  for (auto* a = first; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      if (u->Address.lpSockaddr->sa_family != AF_INET || u->OnLinkPrefixLength >= 31) continue;
      const auto ip = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr;
      const ULONG mask = u->OnLinkPrefixLength ? htonl(~0u << (32 - u->OnLinkPrefixLength)) : 0;
      in_addr b{};
      b.s_addr = ip.s_addr | ~mask;
      out.push_back(b);
    }
  }
  return out;
}

// Servers answering the search datagram on the networks of this PC, with their info.
std::vector<std::pair<std::string, std::string>> SearchNetworks() {
  std::vector<std::pair<std::string, std::string>> out;
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return out;
  BOOL yes = TRUE;
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof yes);
  sockaddr_in any{};
  any.sin_family = AF_INET;
  bind(s, reinterpret_cast<sockaddr*>(&any), sizeof any);
  for (const in_addr& b : BroadcastAddresses()) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(kPort);
    to.sin_addr = b;
    sendto(s, "DCCOOP?", 7, 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
  }
  const auto start = Clock::now();
  char buf[8192];
  for (;;) {
    const int left = 700 - int(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    if (left <= 0) break;
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    timeval t{0, left * 1000};
    if (select(0, &r, nullptr, nullptr, &t) != 1) break;
    sockaddr_in from{};
    int len = sizeof from;
    const int n = recvfrom(s, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
    if (n <= 7 || memcmp(buf, "DCCOOP!", 7)) continue;
    char ip[INET_ADDRSTRLEN] = "";
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
    out.push_back({ip, std::string(buf + 7, n - 7)});
  }
  closesocket(s);
  return out;
}

bool ParseInfo(const std::string& address, const std::string& text, int ms, FoundServer* out) {
  try {
    const Json info = Json::parse(text);
    if (!info.is_object() || !info.contains("name")) return false;
    out->address = Widen(address);
    out->name = Widen(info.value("name", address));
    out->players = info.contains("players") && info["players"].is_array() ? int(info["players"].size()) : 0;
    out->version = Widen(info.value("version", ""));
    out->ping_ms = ms;
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

std::vector<FoundServer> FindServers(const std::vector<std::wstring>& known) {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  std::mutex lock;
  std::vector<FoundServer> found;
  std::set<std::string> asked;
  // a round: ask each new address for its info and the servers listed on it
  const auto ask = [&](const std::vector<std::string>& addresses) {
    std::vector<std::string> more;
    std::vector<std::thread> threads;
    for (const std::string& a : addresses) {
      {
        std::lock_guard<std::mutex> l(lock);
        if (!IsIPv4(a) || !asked.insert(a).second) continue;
      }
      threads.emplace_back([&, a] {
        std::string body;
        int ms = 0;
        FoundServer s;
        if (!HttpGet(a, "/dccoop/info", &body, &ms) || !ParseInfo(a, body, ms, &s)) return;
        // the ping: the info request's connection and answer; a second, warm request is closer to the truth
        int ms2 = 0;
        std::string again;
        if (HttpGet(a, "/dccoop/info", &again, &ms2)) s.ping_ms = (std::min)(ms, ms2);
        std::string list;
        std::vector<std::string> listed;
        if (HttpGet(a, "/dccoop/servers", &list, nullptr)) {
          try {
            for (const auto& e : Json::parse(list)) {
              const std::string at = e.value("address", "");
              if (!at.empty() && e.value("port", kPort) == kPort) listed.push_back(at);
            }
          } catch (...) {
          }
        }
        std::lock_guard<std::mutex> l(lock);
        found.push_back(s);
        more.insert(more.end(), listed.begin(), listed.end());
      });
    }
    for (auto& t : threads) t.join();
    return more;
  };
  std::vector<std::string> first;
  for (const auto& k : known) first.push_back(Narrow(k));
  std::thread tailscale([&] {
    const auto peers = TailscalePeers();
    std::lock_guard<std::mutex> l(lock);
    first.insert(first.end(), peers.begin(), peers.end());
  });
  const auto answered = SearchNetworks();
  tailscale.join();
  for (const auto& [ip, info] : answered) first.push_back(ip);
  std::vector<std::string> next = ask(first);
  for (int round = 0; round < 2 && !next.empty(); ++round) next = ask(next);
  WSACleanup();
  // the same server on two networks (home network and VPN): the one that answered fastest
  std::sort(found.begin(), found.end(), [](const FoundServer& a, const FoundServer& b) { return a.ping_ms < b.ping_ms; });
  std::vector<FoundServer> out;
  for (const auto& s : found) {
    const bool twice = std::any_of(out.begin(), out.end(), [&](const FoundServer& o) {
      return o.name == s.name && o.version == s.version && o.players == s.players;
    });
    if (!twice) out.push_back(s);
  }
  std::stable_sort(out.begin(), out.end(), [](const FoundServer& a, const FoundServer& b) { return a.players > b.players; });
  return out;
}

std::wstring DescribeServer(const FoundServer& s) {
  return s.name + L"  -  " + std::to_wstring(s.players) + L" playing  -  " + s.address + L"  -  " +
         std::to_wstring(s.ping_ms) + L" ms";
}

void SaveServers(const std::wstring& path, const std::vector<FoundServer>& list) {
  Json out = Json::array();
  for (const auto& s : list)
    out.push_back({{"address", Narrow(s.address)}, {"name", Narrow(s.name)}, {"players", s.players}, {"ping", s.ping_ms},
                   {"version", Narrow(s.version)}});
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  const long long stamp = ((long long)now.dwHighDateTime << 32 | now.dwLowDateTime) / 10000;
  std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
  f << Json{{"time", stamp}, {"servers", out}}.dump();
}

std::vector<FoundServer> LoadServers(const std::wstring& path, long long* age_ms) {
  std::vector<FoundServer> out;
  if (age_ms) *age_ms = -1;
  std::ifstream f(path.c_str(), std::ios::binary);
  if (!f) return out;
  try {
    const Json in = Json::parse(f);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const long long stamp = ((long long)now.dwHighDateTime << 32 | now.dwLowDateTime) / 10000;
    if (age_ms) *age_ms = stamp - in.value("time", 0LL);
    for (const auto& e : in.value("servers", Json::array())) {
      FoundServer s;
      s.address = Widen(e.value("address", ""));
      s.name = Widen(e.value("name", ""));
      s.players = e.value("players", 0);
      s.ping_ms = e.value("ping", 0);
      s.version = Widen(e.value("version", ""));
      if (!s.address.empty()) out.push_back(s);
    }
  } catch (...) {
  }
  return out;
}
