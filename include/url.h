#pragma once

#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dixelu
{

struct parsed_url
{
	std::string scheme;
	std::string host;
	std::string port;
	std::string target;
	bool tls = false;
};

inline parsed_url parse_url(std::string_view url)
{
	parsed_url result;
	const auto scheme_end = url.find("://");
	if (scheme_end == std::string_view::npos || scheme_end == 0)
		throw std::invalid_argument("URL has no scheme");

	result.scheme.assign(url.substr(0, scheme_end));
	for (char& character : result.scheme)
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	result.tls = result.scheme == "https" || result.scheme == "wss";
	url.remove_prefix(scheme_end + 3);

	const auto target_start = url.find_first_of("/?#");
	const std::string_view authority = target_start == std::string_view::npos
		? url
		: url.substr(0, target_start);
	std::string_view target = target_start == std::string_view::npos
		? std::string_view{}
		: url.substr(target_start);

	if (const auto fragment = target.find('#'); fragment != std::string_view::npos)
		target = target.substr(0, fragment);
	if (target.empty())
		result.target = "/";
	else if (target.front() == '?')
		result.target = "/" + std::string(target);
	else
		result.target.assign(target);

	if (authority.empty() || authority.find('@') != std::string_view::npos)
		throw std::invalid_argument("URL has invalid authority");

	if (authority.front() == '[')
	{
		const auto closing = authority.find(']');
		if (closing == std::string_view::npos || closing == 1)
			throw std::invalid_argument("URL has invalid IPv6 authority");
		result.host.assign(authority.substr(1, closing - 1));
		const auto suffix = authority.substr(closing + 1);
		if (!suffix.empty())
		{
			if (suffix.front() != ':' || suffix.size() == 1)
				throw std::invalid_argument("URL has invalid IPv6 port");
			result.port.assign(suffix.substr(1));
		}
	}
	else
	{
		const auto colon = authority.rfind(':');
		if (colon == std::string_view::npos)
			result.host.assign(authority);
		else
		{
			if (authority.find(':') != colon || colon == 0 || colon + 1 == authority.size())
				throw std::invalid_argument("URL has invalid host or port");
			result.host.assign(authority.substr(0, colon));
			result.port.assign(authority.substr(colon + 1));
		}
	}

	if (result.host.empty())
		throw std::invalid_argument("URL has no host");
	if (result.port.empty())
		result.port = result.tls ? "443" : "80";
	unsigned port_number = 0;
	for (const char raw_character : result.port)
	{
		const auto character = static_cast<unsigned char>(raw_character);
		if (!std::isdigit(character))
			throw std::invalid_argument("URL port is not numeric");
		port_number = port_number * 10 + static_cast<unsigned>(character - '0');
		if (port_number > 65535)
			throw std::invalid_argument("URL port is out of range");
	}

	return result;
}

inline std::string url_encode(std::string_view input)
{
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string result;
	result.reserve(input.size() * 3);
	for (const char raw_character : input)
	{
		const auto character = static_cast<unsigned char>(raw_character);
		if ((character >= 'A' && character <= 'Z') ||
			(character >= 'a' && character <= 'z') ||
			(character >= '0' && character <= '9') ||
			character == '-' || character == '_' || character == '.' || character == '~')
		{
			result.push_back(static_cast<char>(character));
		}
		else
		{
			result.push_back('%');
			result.push_back(hex[character >> 4]);
			result.push_back(hex[character & 0x0F]);
		}
	}
	return result;
}

} // namespace dixelu
