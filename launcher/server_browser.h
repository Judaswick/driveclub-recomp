// The server list of "Join a session": co-op servers found on their own (docs/online).
#pragma once

#include <string>
#include <vector>

struct FoundServer {
  std::wstring address;  // IPv4 the game connects to (port 8080)
  std::wstring name;     // the server's name (server.name, else its computer's name)
  int players = 0;       // playing now
  int ping_ms = 0;       // the info request's round trip
  std::wstring version;
};

// Looks for servers, at most about two seconds: the known addresses (recently joined, the one set), the Tailscale
// peers, an answer to a search datagram on every network (home network, Hamachi, Radmin), and the lists of the
// servers found (a server can keep a list of others: server.list_on). Sorted by players, then ping.
std::vector<FoundServer> FindServers(const std::vector<std::wstring>& known);

// The list as the launcher shows it in its "Servers" row.
std::wstring DescribeServer(const FoundServer& s);

// Kept between the launcher's short-lived UI processes: <dir>\servers.json.
void SaveServers(const std::wstring& path, const std::vector<FoundServer>& list);
std::vector<FoundServer> LoadServers(const std::wstring& path, long long* age_ms);
