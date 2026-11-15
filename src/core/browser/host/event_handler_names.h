#pragma once

#include <string>
#include <vector>

namespace iv8::browser {

enum class EventHandlerTarget { kWindow, kDocument, kHTMLElement };

// Chrome 157.0.8103.51 reference lists. HTMLElement includes the eight inherited
// Element handlers; event_handler_interface retains their defining prototype.
const std::vector<std::string>& event_handler_names(EventHandlerTarget target);

bool is_event_handler_name(EventHandlerTarget target, const std::string& name);

const char* event_handler_interface(EventHandlerTarget target,
                                    const std::string& name);

}  // namespace iv8::browser
