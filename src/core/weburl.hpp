// Qt-free helpers for web links (Twitch chat, Stream Manager, custom pages).
#pragma once
#include "layouts.hpp"
#include <cctype>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace ovl::web {

struct Preset {
	const char *name, *input, *error; // input == nullptr: no text field needed
};
inline const std::vector<Preset> &presets()
{
	static const std::vector<Preset> p = {
		{"Twitch chat", "Twitch channel name", "Enter a valid Twitch channel name (3-25 letters, digits or _)."},
		{"Twitch Stream Manager", nullptr, ""},
		{"Custom URL", "https://example.com/page", "Enter a valid http(s) address."}};
	return p;
}

struct Parts {
	std::string scheme, host;
};

// scheme://[user@]host[:port]/... -> scheme + host (lower-cased); nullopt when it is not a usable absolute URL
inline std::optional<Parts> parse(const std::string &url)
{
	size_t p = url.find("://");
	if (p == std::string::npos || p == 0) return std::nullopt;
	Parts out;
	out.scheme = layouts::lower(url.substr(0, p));
	size_t b = p + 3, e = url.find_first_of("/?#", b);
	std::string auth = url.substr(b, e == std::string::npos ? std::string::npos : e - b);
	size_t at = auth.rfind('@');
	if (at != std::string::npos) auth = auth.substr(at + 1);
	if (!auth.empty() && auth[0] == '[') {
		size_t close = auth.find(']');
		if (close == std::string::npos) return std::nullopt;
		out.host = auth.substr(1, close - 1);
	} else {
		size_t colon = auth.find(':');
		out.host = auth.substr(0, colon);
	}
	out.host = layouts::lower(out.host);
	if (auth.empty()) return std::nullopt;
	return out;
}

// Accepts "example.com/x" or full http(s) URLs. Rejects every other scheme (file:, javascript:, ...).
inline std::optional<std::string> normalize_url(const std::string &text)
{
	size_t a = 0, z = text.size();
	while (a < z && std::isspace((unsigned char)text[a])) ++a;
	while (z > a && std::isspace((unsigned char)text[z - 1])) --z;
	std::string t = text.substr(a, z - a);
	if (t.empty()) return std::nullopt;
	for (char c : t) if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0') return std::nullopt;
	if (t.find("://") == std::string::npos) {
		std::string first = t.substr(0, t.find('/'));
		static const std::regex hostport(R"(^[^/]+:\d+(/|$))");
		if (first.find(':') != std::string::npos && !std::regex_search(t, hostport)) return std::nullopt; // javascript:..., mailto:...
		t = "https://" + t;
	}
	auto u = parse(t);
	if (!u || (u->scheme != "http" && u->scheme != "https") || u->host.empty()) return std::nullopt;
	return t;
}

inline std::string host_of(const std::string &url)
{
	auto u = parse(url);
	if (!u || u->host.empty()) return "Web link";
	return u->host.rfind("www.", 0) == 0 ? u->host.substr(4) : u->host;
}

inline std::optional<std::string> preset_url(const std::string &name, const std::string &text)
{
	if (name == "Twitch chat") {
		size_t a = 0;
		std::string ch = text;
		while (!ch.empty() && std::isspace((unsigned char)ch.front())) ch.erase(ch.begin());
		while (!ch.empty() && std::isspace((unsigned char)ch.back())) ch.pop_back();
		while (!ch.empty() && (ch[0] == '#' || ch[0] == '@')) ch.erase(ch.begin());
		(void)a;
		static const std::regex chan("^[A-Za-z0-9_]{3,25}$");
		if (!std::regex_match(ch, chan)) return std::nullopt;
		return "https://www.twitch.tv/popout/" + layouts::lower(ch) + "/chat?popout=";
	}
	if (name == "Twitch Stream Manager") return std::string("https://dashboard.twitch.tv/stream-manager");
	return normalize_url(text);
}

} // namespace ovl::web
