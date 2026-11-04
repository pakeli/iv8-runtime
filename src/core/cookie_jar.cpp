#include "iv8/cookie_jar.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>

namespace iv8 {

namespace {

// M11-46: deterministic document.cookie parsing helpers. Cookie text is kept
// byte-for-byte (no percent decoding); only ASCII OWS trimming and attribute
// name normalization are applied. There is no wall clock: expiry is compared to
// the fixed Unix epoch 1970-01-01T00:00:00Z, so a cookie is deleted only when it
// specifies max-age <= 0 or an expires at/before that epoch.
std::string cookie_trim(std::string value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' ||
            value[begin] == '\r' || value[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                           value[end - 1] == '\r' ||
                           value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string cookie_ascii_lower(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

long long cookie_days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const int adjusted_month =
        static_cast<int>(month) + (month > 2 ? -3 : 9);
    const unsigned doy =
        (153 * adjusted_month + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<long long>(era) * 146097 + static_cast<long long>(doe) -
           719468;
}

bool cookie_parse_http_date(const std::string& text, long long* epoch) {
    std::tm tm{};
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
    if (input.fail()) {
        input.clear();
        input.str(text);
        input.imbue(std::locale::classic());
        input >> std::get_time(&tm, "%a, %d-%b-%Y %H:%M:%S");
    }
    if (input.fail()) {
        input.clear();
        input.str(text);
        input.imbue(std::locale::classic());
        input >> std::get_time(&tm, "%a %b %d %H:%M:%S %Y");
    }
    if (input.fail()) {
        return false;
    }
    const int year = tm.tm_year + 1900;
    const unsigned month = static_cast<unsigned>(tm.tm_mon + 1);
    const unsigned day = static_cast<unsigned>(tm.tm_mday);
    if (month < 1 || month > 12 || day < 1 || day > 31 || tm.tm_hour < 0 ||
        tm.tm_hour > 23 || tm.tm_min < 0 || tm.tm_min > 59 || tm.tm_sec < 0 ||
        tm.tm_sec > 59) {
        return false;
    }
    const long long days = cookie_days_from_civil(year, month, day);
    *epoch = days * 86400 + static_cast<long long>(tm.tm_hour) * 3600 +
             static_cast<long long>(tm.tm_min) * 60 + tm.tm_sec;
    return true;
}

bool cookie_parse_int64(const std::string& text, long long* value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || end == nullptr || *end != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

}  // namespace

std::string CookieJar::cookie_string() const {
    std::string result;
    for (const auto& entry : entries_) {
        if (!result.empty()) {
            result += "; ";
        }
        result += entry.first;
        result += '=';
        result += entry.second;
    }
    return result;
}

void CookieJar::set_cookie(const std::string& value) {
    const std::string header = cookie_trim(value);
    if (header.empty()) {
        return;
    }
    const std::size_t first_separator = header.find(';');
    const std::string pair = cookie_trim(header.substr(0, first_separator));
    const std::size_t equals = pair.find('=');
    if (equals == std::string::npos) {
        return;
    }
    const std::string name = cookie_trim(pair.substr(0, equals));
    const std::string cookie_value = cookie_trim(pair.substr(equals + 1));
    if (name.empty()) {
        return;
    }

    bool has_max_age = false;
    bool max_age_deletes = false;
    bool expires_deletes = false;
    if (first_separator != std::string::npos) {
        std::size_t position = first_separator + 1;
        while (position <= header.size()) {
            const std::size_t next = header.find(';', position);
            std::string attribute = cookie_trim(header.substr(
                position, next == std::string::npos ? std::string::npos
                                                    : next - position));
            if (!attribute.empty()) {
                const std::size_t attr_equals = attribute.find('=');
                const std::string attr_name = cookie_ascii_lower(cookie_trim(
                    attribute.substr(0, attr_equals)));
                const std::string attr_value =
                    attr_equals == std::string::npos
                        ? std::string()
                        : cookie_trim(attribute.substr(attr_equals + 1));
                if (attr_name == "max-age") {
                    long long seconds = 0;
                    if (cookie_parse_int64(attr_value, &seconds)) {
                        has_max_age = true;
                        max_age_deletes = seconds <= 0;
                    }
                } else if (attr_name == "expires") {
                    long long epoch = 0;
                    if (cookie_parse_http_date(attr_value, &epoch)) {
                        expires_deletes = epoch <= 0;
                    }
                }
                // path / domain / secure / samesite and unknown attributes are
                // accepted but intentionally ignored in this deterministic jar.
            }
            if (next == std::string::npos) {
                break;
            }
            position = next + 1;
        }
    }

    const bool should_delete = has_max_age ? max_age_deletes : expires_deletes;
    if (should_delete) {
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                           [&name](const auto& entry) {
                               return entry.first == name;
                           }),
            entries_.end());
        return;
    }
    for (auto& entry : entries_) {
        if (entry.first == name) {
            entry.second = cookie_value;
            return;
        }
    }
    entries_.emplace_back(name, cookie_value);
}

}  // namespace iv8
