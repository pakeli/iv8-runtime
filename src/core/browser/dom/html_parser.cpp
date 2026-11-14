#include "html_parser.h"

#include <cctype>
#include <utility>
#include "iv8/html_entities.h"
#include "../internal/text.h"

namespace iv8::browser {

bool is_void_element(const std::string& tag) {
    static const char* const kVoid[] = {"area", "base", "br",    "col",
                                        "embed", "hr", "img",    "input",
                                        "link",  "meta", "param", "source",
                                        "track", "wbr"};
    for (const char* v : kVoid) {
        if (tag == v) {
            return true;
        }
    }
    return false;
}

// Split a class attribute value into tokens on ASCII whitespace.
std::vector<std::string> split_class_tokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::size_t j = 0;
    const std::size_t m = value.size();
    while (j < m) {
        while (j < m && std::isspace(static_cast<unsigned char>(value[j]))) j++;
        const std::size_t ts = j;
        while (j < m && !std::isspace(static_cast<unsigned char>(value[j]))) j++;
        if (j > ts) tokens.push_back(value.substr(ts, j - ts));
    }
    return tokens;
}

// Capture id + class (+ M3-5 src) from the raw attribute text inside a start tag.
// Minimal attribute tokenizer (quoted / bare values). Only id, class, and src are
// retained — every other attribute is ignored. getAttribute still answers only id
// and class (M2-7/M2-8); src is captured solely for `<script src>` resolution and
// is NOT exposed through getAttribute.
void parse_attributes(const std::string& attrs, DomNode& node) {
    const std::size_t n = attrs.size();
    std::size_t i = 0;
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(attrs[i]))) i++;
        if (i >= n) break;
        const std::size_t name_start = i;
        while (i < n && attrs[i] != '=' && attrs[i] != '/' &&
               !std::isspace(static_cast<unsigned char>(attrs[i])))
            i++;
        const std::string name = ascii_lower(attrs.substr(name_start, i - name_start));
        std::string value;
        while (i < n && std::isspace(static_cast<unsigned char>(attrs[i]))) i++;
        if (i < n && attrs[i] == '=') {
            i++;
            while (i < n && std::isspace(static_cast<unsigned char>(attrs[i]))) i++;
            if (i < n && (attrs[i] == '"' || attrs[i] == '\'')) {
                const char quote = attrs[i++];
                const std::size_t vs = i;
                while (i < n && attrs[i] != quote) i++;
                value = attrs.substr(vs, i - vs);
                if (i < n) i++;  // closing quote
            } else {
                const std::size_t vs = i;
                while (i < n && attrs[i] != '/' &&
                       !std::isspace(static_cast<unsigned char>(attrs[i])))
                    i++;
                value = attrs.substr(vs, i - vs);
            }
        }
        if (name.empty()) {
            continue;  // malformed: ignore an empty attribute name
        }
        // M11-49: decode character references in attribute values (raw name
        // syntax is unchanged).
        value = decode_html_entities(value);
        if (name == "id") {
            node.id = value;
            node.has_id = true;
        } else if (name == "class") {
            node.class_name = value;
            node.has_class = true;
            node.classes = split_class_tokens(value);
        } else {
            if (name == "src") {  // M3-5 keeps a dedicated src for resolution
                node.src = value;
                node.has_src = true;
            }
            // M3-8: every non-id/class attribute is readable via getAttribute.
            // operator[] overwrites, so a duplicate name keeps the last value.
            node.attributes[name] = value;
        }
    }
}

// Find the next case-insensitive "</tag" whose name is followed by whitespace
// or '>', at or after `from`; npos when absent. Used for raw-text and RCDATA
// elements so their bodies are not tag-parsed.
std::size_t find_raw_text_close(const std::string& html, std::size_t from,
                                const std::string& tag) {
    const std::size_t n = html.size();
    const std::size_t tag_len = tag.size();
    for (std::size_t p = from; p + 2 + tag_len <= n; ++p) {
        if (html[p] != '<' || html[p + 1] != '/') {
            continue;
        }
        bool match = true;
        for (std::size_t q = 0; q < tag_len; ++q) {
            const char c = static_cast<char>(std::tolower(
                static_cast<unsigned char>(html[p + 2 + q])));
            if (c != tag[q]) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }
        const std::size_t after = p + 2 + tag_len;
        if (after >= n || html[after] == '>' ||
            std::isspace(static_cast<unsigned char>(html[after]))) {
            return p;
        }
    }
    return std::string::npos;
}

// Attach a freshly created node to the current parse position (top level when
// the stack is empty, otherwise the innermost open element).
DomNode* attach_parsed_node(std::vector<std::unique_ptr<DomNode>>& pool,
                            std::vector<DomNode*>& roots,
                            std::vector<DomNode*>& stack,
                            std::unique_ptr<DomNode> node) {
    DomNode* raw = node.get();
    pool.push_back(std::move(node));
    if (stack.empty()) {
        roots.push_back(raw);
    } else {
        raw->parent = stack.back();
        stack.back()->children.push_back(raw);
    }
    return raw;
}

// M11-48: emit a parsed Text node, merging with an immediately preceding Text
// sibling so adjacent raw runs do not produce multiple text nodes.
void append_parsed_text(std::vector<std::unique_ptr<DomNode>>& pool,
                        std::vector<DomNode*>& roots,
                        std::vector<DomNode*>& stack,
                        const std::string& raw_text) {
    if (raw_text.empty()) {
        return;
    }
    const std::string text = decode_html_entities(raw_text);
    DomNode* last = nullptr;
    if (stack.empty()) {
        if (!roots.empty()) last = roots.back();
    } else if (!stack.back()->children.empty()) {
        last = stack.back()->children.back();
    }
    if (last != nullptr && last->kind == NodeKind::Text) {
        last->data += text;
        return;
    }
    auto node = std::make_unique<DomNode>();
    node->kind = NodeKind::Text;
    node->data = text;
    (void)attach_parsed_node(pool, roots, stack, std::move(node));
}

void append_parsed_comment(std::vector<std::unique_ptr<DomNode>>& pool,
                           std::vector<DomNode*>& roots,
                           std::vector<DomNode*>& stack,
                           const std::string& text) {
    auto node = std::make_unique<DomNode>();
    node->kind = NodeKind::Comment;
    node->data = text;
    (void)attach_parsed_node(pool, roots, stack, std::move(node));
}

// Minimal tag-stack HTML parse into a node forest. M11-48: text and comment
// nodes are emitted in source order; doctype/other declarations are skipped.
// NOT HTML5-conformant. M3-5: <script> (and M11-48 <style>/<title>/<textarea>)
// are raw-text/RCDATA elements whose bodies are captured verbatim, and every
// script is appended to `scripts` in document order.
void parse_html(const std::string& html,
                std::vector<std::unique_ptr<DomNode>>& pool,
                std::vector<DomNode*>& roots,
                std::vector<ScriptRecord>& scripts) {
    std::vector<DomNode*> stack;
    const std::size_t n = html.size();
    std::size_t i = 0;
    while (i < n) {
        if (html[i] != '<') {
            const std::size_t next = html.find('<', i);
            const std::size_t end = (next == std::string::npos) ? n : next;
            append_parsed_text(pool, roots, stack,
                               html.substr(i, end - i));
            i = end;
            continue;
        }
        if (i + 1 >= n) {
            append_parsed_text(pool, roots, stack, "<");
            break;
        }
        const char c1 = html[i + 1];
        if (c1 == '/') {  // end tag
            std::size_t j = i + 2;
            const std::size_t ns = j;
            while (j < n && html[j] != '>' &&
                   !std::isspace(static_cast<unsigned char>(html[j])))
                j++;
            const std::string name = ascii_lower(html.substr(ns, j - ns));
            while (j < n && html[j] != '>') j++;
            if (j < n) j++;
            i = j;
            for (std::size_t k = stack.size(); k-- > 0;) {
                if (stack[k]->tag == name) {
                    stack.resize(k);
                    break;
                }
            }
            continue;
        }
        if (c1 == '!') {  // comment / doctype / other declaration
            if (html.compare(i, 4, "<!--") == 0) {
                const std::size_t close = html.find("-->", i + 4);
                const std::size_t data_end =
                    (close == std::string::npos) ? n : close;
                append_parsed_comment(
                    pool, roots, stack,
                    html.substr(i + 4, data_end - (i + 4)));
                i = (close == std::string::npos) ? n : close + 3;
                continue;
            }
            std::size_t j = i + 2;
            while (j < n && html[j] != '>') j++;
            if (j < n) j++;
            i = j;
            continue;
        }
        if (c1 == '?') {  // processing-instruction-like; skip as a bogus comment
            std::size_t j = i + 2;
            while (j < n && html[j] != '>') j++;
            if (j < n) j++;
            i = j;
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(c1))) {
            // A literal '<' that cannot start a tag is text (e.g. "a < b").
            append_parsed_text(pool, roots, stack, "<");
            i++;
            continue;
        }
        // start tag
        std::size_t j = i + 1;
        const std::size_t ns = j;
        while (j < n && html[j] != '>' && html[j] != '/' &&
               !std::isspace(static_cast<unsigned char>(html[j])))
            j++;
        const std::string tag = ascii_lower(html.substr(ns, j - ns));
        const std::size_t body_start = j;
        while (j < n && html[j] != '>') j++;
        std::string body = html.substr(body_start, j - body_start);
        const bool self_closing = !body.empty() && body.back() == '/';
        if (self_closing) body.pop_back();
        if (j < n) j++;  // past '>'
        i = j;
        if (tag.empty()) continue;

        auto node = std::make_unique<DomNode>();
        node->tag = tag;
        parse_attributes(body, *node);
        // M5-3 / M5-6: seed <input>/<button>.value once from the parsed `value`
        // attribute (absent -> ""). Thereafter the runtime .value slot is decoupled
        // from the attribute.
        if (tag == "input" || tag == "button") {
            const auto vit = node->attributes.find("value");
            if (vit != node->attributes.end()) {
                node->value = vit->second;
            }
        }
        // M5-5: seed <option>.selected once from the boolean `selected` attribute
        // (present -> true). Decoupled from the attribute thereafter.
        if (tag == "option") {
            node->selected =
                node->attributes.find("selected") != node->attributes.end();
        }
        // M5-7: seed <input>.checked once from the boolean `checked` attribute
        // (present -> true). Decoupled from the attribute thereafter.
        if (tag == "input") {
            node->checked =
                node->attributes.find("checked") != node->attributes.end();
        }
        DomNode* raw = attach_parsed_node(pool, roots, stack, std::move(node));

        // M3-5/M11-48: raw-text (script, style) and RCDATA (title, textarea)
        // elements consume their body verbatim. A Text child represents the
        // content; the elements are not pushed on the tag stack.
        if (!self_closing &&
            (tag == "script" || tag == "style" || tag == "title" ||
             tag == "textarea")) {
            const std::size_t close = find_raw_text_close(html, i, tag);
            const std::size_t content_end =
                (close == std::string::npos) ? n : close;
            if (content_end > i) {
                auto text = std::make_unique<DomNode>();
                text->kind = NodeKind::Text;
                // script/style are raw text; title/textarea are RCDATA and decode
                // character references.
                const bool raw_text = tag == "script" || tag == "style";
                text->data = raw_text
                                 ? html.substr(i, content_end - i)
                                 : decode_html_entities(
                                       html.substr(i, content_end - i));
                text->parent = raw;
                raw->children.push_back(text.get());
                pool.push_back(std::move(text));
            }
            if (tag == "script") {
                ScriptRecord record;
                record.has_src = raw->has_src;
                record.src = raw->src;
                record.node = raw;  // M3-7: backing node for document.currentScript
                if (!raw->has_src) {  // inline: capture the raw JS source
                    record.code = html.substr(i, content_end - i);
                }
                scripts.push_back(std::move(record));
            }
            // Advance past the </tag ...> end tag.
            std::size_t k = (close == std::string::npos) ? n : close;
            while (k < n && html[k] != '>') k++;
            if (k < n) k++;
            i = k;
            continue;
        }

        if (!self_closing && !is_void_element(tag)) {
            stack.push_back(raw);
        }
    }

    // M5-4: seed <textarea>.value once from its initial text content (empty ->
    // ""). Thereafter the runtime .value slot is decoupled from textContent.
    for (const std::unique_ptr<DomNode>& node : pool) {
        if (node->kind == NodeKind::Element && node->tag == "textarea") {
            node->value = text_content_of(node.get());
        }
    }
}

// M3-10: whether a <script> is a minimal *classic* (executable) script, by its
// raw `type` attribute. Executable iff: no type attribute, type is empty or only
// ASCII whitespace, or type (trimmed, ASCII-lowercased) is exactly
// "text/javascript" or "application/javascript". Everything else (module,
// importmap, application/json, text/plain, any other non-empty type) is
// non-executable — it stays in the DOM / document.scripts but does not run.
bool is_executable_classic_script(const DomNode* node) {
    const auto it = node->attributes.find("type");
    if (it == node->attributes.end()) {
        return true;  // no type attribute -> classic
    }
    const std::string& raw = it->second;
    std::size_t a = 0;
    std::size_t b = raw.size();
    while (a < b && std::isspace(static_cast<unsigned char>(raw[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(raw[b - 1]))) b--;
    if (a == b) {
        return true;  // empty or whitespace-only -> classic
    }
    const std::string type = ascii_lower(raw.substr(a, b - a));
    return type == "text/javascript" || type == "application/javascript";
}

}  // namespace iv8::browser
