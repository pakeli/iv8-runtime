#include "common.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace iv8::browser {

TraceTokenAddresses trace_token_addresses_in_common_tu() noexcept {
    return IV8_BROWSER_LOCAL_TRACE_TOKEN_ADDRESSES();
}

void verify_trace_token_identity(const TraceTokenAddresses& local) {
    if (local != trace_token_addresses_in_common_tu()) {
        throw std::logic_error(
            "iv8 internal error: access-trace identity tokens resolve to different "
            "addresses in different translation units (they must be inline "
            "variables; see src/core/browser/internal/common.h)");
    }
}

}  // namespace iv8::browser
