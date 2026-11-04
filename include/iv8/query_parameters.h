#pragma once

#include <string>
#include <utility>
#include <vector>

namespace iv8 {

// Internal ordered byte-string pairs. The caller owns all V8 state/coercion.
using QueryParameterList = std::vector<std::pair<std::string, std::string>>;

// Parse raw query text; the URLSearchParams constructor strips a leading '?'.
QueryParameterList parse_query_parameters(const std::string& query);
std::string serialize_query_parameters(const QueryParameterList& pairs);

}  // namespace iv8
