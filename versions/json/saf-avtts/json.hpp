// Small helpers over Boost.JSON for concise field access with clear errors.
#ifndef MAXCALLS_DETAIL_JSON_HPP
#define MAXCALLS_DETAIL_JSON_HPP

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <string>

#include "maxcalls/maxcalls.hpp"

namespace maxcalls::detail {

namespace json = boost::json;

inline json::value parse_json(std::string_view text) {
    boost::system::error_code ec;
    json::value v = json::parse(text, ec);
    if (ec) throw Error("invalid JSON: " + ec.message());
    return v;
}

// Returns object member `key` as a string, or throws.
inline std::string require_string(const json::object& obj, std::string_view key) {
    auto* v = obj.if_contains(key);
    if (!v || !v->is_string()) throw Error("missing string field: " + std::string(key));
    return std::string(v->as_string().c_str(), v->as_string().size());
}

// Returns object member `key` as a string if present and a string, else "".
inline std::string optional_string(const json::object& obj, std::string_view key) {
    auto* v = obj.if_contains(key);
    if (!v || !v->is_string()) return {};
    return std::string(v->as_string().c_str(), v->as_string().size());
}

inline const json::object& require_object(const json::value& v, std::string_view what) {
    if (!v.is_object()) throw Error("expected JSON object: " + std::string(what));
    return v.as_object();
}

} // namespace maxcalls::detail

#endif // MAXCALLS_DETAIL_JSON_HPP
