#include "event_handler_names.h"

#include <algorithm>

namespace iv8::browser {

namespace {

// Source: Chrome 157.0.8103.51 member inventory captured on 2026-11-15.
// These sorted lists contain only observed on* properties; they do not cause
// device events, navigation, network activity, or default browser actions.

const std::vector<std::string> kWindowHandlerNames = {
    "onabort", "onafterprint", "onanimationcancel",
    "onanimationend", "onanimationiteration", "onanimationstart",
    "onappinstalled", "onauxclick", "onbeforeinput",
    "onbeforeinstallprompt", "onbeforematch", "onbeforeprint",
    "onbeforetoggle", "onbeforeunload", "onbeforexrselect",
    "onblur", "oncancel", "oncanplay",
    "oncanplaythrough", "onchange", "onclick",
    "onclose", "oncommand", "oncontentvisibilityautostatechange",
    "oncontextlost", "oncontextmenu", "oncontextrestored",
    "oncuechange", "ondblclick", "ondevicemotion",
    "ondeviceorientation", "ondeviceorientationabsolute", "ondrag",
    "ondragend", "ondragenter", "ondragleave",
    "ondragover", "ondragstart", "ondrop",
    "ondurationchange", "onemptied", "onended",
    "onerror", "onfocus", "onformdata",
    "ongamepadconnected", "ongamepaddisconnected", "ongotpointercapture",
    "onhashchange", "oninput", "oninvalid",
    "onkeydown", "onkeypress", "onkeyup",
    "onlanguagechange", "onload", "onloadeddata",
    "onloadedmetadata", "onloadstart", "onlostpointercapture",
    "onmessage", "onmessageerror", "onmousedown",
    "onmouseenter", "onmouseleave", "onmousemove",
    "onmouseout", "onmouseover", "onmouseup",
    "onmousewheel", "onoffline", "ononline",
    "onpagehide", "onpagereveal", "onpageshow",
    "onpageswap", "onpause", "onplay",
    "onplaying", "onpointercancel", "onpointerdown",
    "onpointerenter", "onpointerleave", "onpointermove",
    "onpointerout", "onpointerover", "onpointerrawupdate",
    "onpointerup", "onpopstate", "onprogress",
    "onratechange", "onrejectionhandled", "onreset",
    "onresize", "onscroll", "onscrollend",
    "onscrollsnapchange", "onscrollsnapchanging", "onsearch",
    "onsecuritypolicyviolation", "onseeked", "onseeking",
    "onselect", "onselectionchange", "onselectstart",
    "onslotchange", "onstalled", "onstorage",
    "onsubmit", "onsuspend", "ontimeupdate",
    "ontoggle", "ontransitioncancel", "ontransitionend",
    "ontransitionrun", "ontransitionstart", "onunhandledrejection",
    "onunload", "onvolumechange", "onwaiting",
    "onwebkitanimationend", "onwebkitanimationiteration", "onwebkitanimationstart",
    "onwebkittransitionend", "onwheel",
};

const std::vector<std::string> kDocumentHandlerNames = {
    "onabort", "onanimationcancel", "onanimationend",
    "onanimationiteration", "onanimationstart", "onauxclick",
    "onbeforecopy", "onbeforecut", "onbeforeinput",
    "onbeforematch", "onbeforepaste", "onbeforetoggle",
    "onbeforexrselect", "onblur", "oncancel",
    "oncanplay", "oncanplaythrough", "onchange",
    "onclick", "onclose", "oncommand",
    "oncontentvisibilityautostatechange", "oncontextlost", "oncontextmenu",
    "oncontextrestored", "oncopy", "oncuechange",
    "oncut", "ondblclick", "ondrag",
    "ondragend", "ondragenter", "ondragleave",
    "ondragover", "ondragstart", "ondrop",
    "ondurationchange", "onemptied", "onended",
    "onerror", "onfocus", "onformdata",
    "onfreeze", "onfullscreenchange", "onfullscreenerror",
    "ongotpointercapture", "oninput", "oninvalid",
    "onkeydown", "onkeypress", "onkeyup",
    "onload", "onloadeddata", "onloadedmetadata",
    "onloadstart", "onlostpointercapture", "onmousedown",
    "onmouseenter", "onmouseleave", "onmousemove",
    "onmouseout", "onmouseover", "onmouseup",
    "onmousewheel", "onpaste", "onpause",
    "onplay", "onplaying", "onpointercancel",
    "onpointerdown", "onpointerenter", "onpointerleave",
    "onpointerlockchange", "onpointerlockerror", "onpointermove",
    "onpointerout", "onpointerover", "onpointerrawupdate",
    "onpointerup", "onprerenderingchange", "onprogress",
    "onratechange", "onreadystatechange", "onreset",
    "onresize", "onresume", "onscroll",
    "onscrollend", "onscrollsnapchange", "onscrollsnapchanging",
    "onsearch", "onsecuritypolicyviolation", "onseeked",
    "onseeking", "onselect", "onselectionchange",
    "onselectstart", "onslotchange", "onstalled",
    "onsubmit", "onsuspend", "ontimeupdate",
    "ontoggle", "ontransitioncancel", "ontransitionend",
    "ontransitionrun", "ontransitionstart", "onvisibilitychange",
    "onvolumechange", "onwaiting", "onwebkitanimationend",
    "onwebkitanimationiteration", "onwebkitanimationstart", "onwebkitfullscreenchange",
    "onwebkitfullscreenerror", "onwebkittransitionend", "onwheel",
};

const std::vector<std::string> kHTMLElementHandlerNames = {
    "onabort", "onanimationcancel", "onanimationend",
    "onanimationiteration", "onanimationstart", "onauxclick",
    "onbeforecopy", "onbeforecut", "onbeforeinput",
    "onbeforematch", "onbeforepaste", "onbeforetoggle",
    "onbeforexrselect", "onblur", "oncancel",
    "oncanplay", "oncanplaythrough", "onchange",
    "onclick", "onclose", "oncommand",
    "oncontentvisibilityautostatechange", "oncontextlost", "oncontextmenu",
    "oncontextrestored", "oncopy", "oncuechange",
    "oncut", "ondblclick", "ondrag",
    "ondragend", "ondragenter", "ondragleave",
    "ondragover", "ondragstart", "ondrop",
    "ondurationchange", "onemptied", "onended",
    "onerror", "onfocus", "onformdata",
    "onfullscreenchange", "onfullscreenerror", "ongotpointercapture",
    "oninput", "oninvalid", "onkeydown",
    "onkeypress", "onkeyup", "onload",
    "onloadeddata", "onloadedmetadata", "onloadstart",
    "onlostpointercapture", "onmousedown", "onmouseenter",
    "onmouseleave", "onmousemove", "onmouseout",
    "onmouseover", "onmouseup", "onmousewheel",
    "onpaste", "onpause", "onplay",
    "onplaying", "onpointercancel", "onpointerdown",
    "onpointerenter", "onpointerleave", "onpointermove",
    "onpointerout", "onpointerover", "onpointerrawupdate",
    "onpointerup", "onprogress", "onratechange",
    "onreset", "onresize", "onscroll",
    "onscrollend", "onscrollsnapchange", "onscrollsnapchanging",
    "onsearch", "onsecuritypolicyviolation", "onseeked",
    "onseeking", "onselect", "onselectionchange",
    "onselectstart", "onslotchange", "onstalled",
    "onsubmit", "onsuspend", "ontimeupdate",
    "ontoggle", "ontransitioncancel", "ontransitionend",
    "ontransitionrun", "ontransitionstart", "onvolumechange",
    "onwaiting", "onwebkitanimationend", "onwebkitanimationiteration",
    "onwebkitanimationstart", "onwebkitfullscreenchange", "onwebkitfullscreenerror",
    "onwebkittransitionend", "onwheel",
};

const std::vector<std::string> kElementHandlerNames = {
    "onbeforecopy", "onbeforecut", "onbeforepaste",
    "onfullscreenchange", "onfullscreenerror", "onsearch",
    "onwebkitfullscreenchange", "onwebkitfullscreenerror",
};

}  // namespace

const std::vector<std::string>& event_handler_names(EventHandlerTarget target) {
    switch (target) {
        case EventHandlerTarget::kWindow:
            return kWindowHandlerNames;
        case EventHandlerTarget::kDocument:
            return kDocumentHandlerNames;
        case EventHandlerTarget::kHTMLElement:
            return kHTMLElementHandlerNames;
    }
    return kHTMLElementHandlerNames;
}

bool is_event_handler_name(EventHandlerTarget target, const std::string& name) {
    const auto& names = event_handler_names(target);
    return std::binary_search(names.begin(), names.end(), name);
}

const char* event_handler_interface(EventHandlerTarget target,
                                    const std::string& name) {
    if (!is_event_handler_name(target, name)) {
        return "";
    }
    if (target == EventHandlerTarget::kWindow) {
        return "Window";
    }
    if (target == EventHandlerTarget::kDocument) {
        return "Document";
    }
    return std::binary_search(kElementHandlerNames.begin(),
                              kElementHandlerNames.end(), name)
               ? "Element" : "HTMLElement";
}

}  // namespace iv8::browser
