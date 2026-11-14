#!/usr/bin/env python3
"""Move audit for the page_state.cpp split (docs/page_state_split_plan.md, 0.3).

Proves that a "pure move" change relocated C++ code without editing it.

Both sides of a change (a base git ref and a head ref or the working tree) are
read for the same set of files. Each side is tokenized (comments and layout are
dropped), split into namespace-scope entities (a function/class/variable
definition, a declaration, or a preprocessor directive), and the entities are
compared as multisets of normalized token streams. Which file an entity lives
in, and its line number, do not matter; its tokens do.

Gate: the number of *definition* differences (entities with a body or an
initializer). A pure-move PR must report 0. Declarations, preprocessor
directives and namespace changes are listed for review but do not fail the
gate, because a split legitimately adds forward declarations, #includes and
header guards.

Normalization (reported when applied): a leading `inline` / `static` specifier
on a *function* definition is ignored, because moving a file-local function
into a header requires it. Specifiers on variables are never ignored: whether a
namespace-scope variable is `static`, `constexpr` or `inline` decides whether
it has one address program-wide (plan section 3.1).

Parenthesized declarations that cannot be distinguished from variable
initializers without resolving C++ types are conservatively gated as definitions.

Opt-in --class-members CLASS decomposes the named class into its complete
declaration/data contract and separately qualified member definitions. It
preserves access, defaults, qualifiers and initializers while permitting inline
bodies to move out of the class. Ordinary members, constructors and destructors
are supported; unsupported member declarators fail closed. Other classes retain
the original whole-definition gate.

Usage:
    tools/audit_move.py --base master                  # changed C++ files vs working tree
    tools/audit_move.py --base master --head HEAD      # committed head only
    tools/audit_move.py --base REF src/core/page_state.cpp src/core/browser/web/crypto.cpp

Exit status: 0 = no definition differences, 1 = differences, 2 = usage error.
Requires only the Python standard library and git.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import difflib
import re
import subprocess
import sys
from pathlib import Path

CPP_SUFFIXES = (".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".inc")

# --- Tokenizer -----------------------------------------------------------------

_PUNCTUATORS = sorted(
    """
    ->* <<= >>= <=> ... :: -> ++ -- << >> <= >= == != && || += -= *= /= %= &= |= ^=
    .* ## { } [ ] ( ) ; : , . ? ~ ! + - * / % ^ & | = < > #
    """.split(),
    key=len,
    reverse=True,
)
_STRING_PREFIXES = {"u8", "u", "U", "L", "R", "u8R", "uR", "UR", "LR"}
_IDENT_START = re.compile(r"[A-Za-z_]")
_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_NUMBER = re.compile(r"\.?[0-9](?:[0-9A-Za-z_.]|'(?=[0-9A-Za-z])|(?<=[eEpP])[+-])*")


@dataclasses.dataclass(frozen=True)
class Token:
    kind: str  # "ident" | "number" | "string" | "char" | "punct" | "directive"
    text: str
    path: str
    line: int
    start: int = dataclasses.field(default=0, compare=False)
    end: int = dataclasses.field(default=0, compare=False)


class TokenizeError(Exception):
    pass


def _directive_text(raw: str, path: str, line: int) -> str:
    """Normalize directive layout while preserving literal contents and spacing
    between a macro name and its opening parenthesis."""
    out = []
    i = 0

    def err(message):
        return TokenizeError(f"{path}:{line}: {message}")

    while i < len(raw):
        c = raw[i]
        literals = []
        literal_end = _literal_at(raw, i, literals, path, line, err)
        if literal_end is not None:
            i, line = literal_end
            out.append(literals[0].text)
            continue
        word = _IDENT.match(raw, i) or _NUMBER.match(raw, i)
        if word:
            out.append(word.group(0))
            i = word.end()
            continue
        if raw.startswith("\\\n", i):
            line += 1
            i += 2
            continue
        if raw.startswith("//", i):
            break
        if raw.startswith("/*", i):
            end = raw.find("*/", i + 2)
            i = len(raw) if end < 0 else end + 2
            if not out or out[-1] != " ":
                out.append(" ")
            continue
        if c.isspace():
            if not out or out[-1] != " ":
                out.append(" ")
        else:
            out.append(c)
        i += 1
    text = "".join(out).strip()
    # "# include" and "#include" are the same directive.
    return re.sub(r"^#\s*", "#", text)


def tokenize(source: str, path: str) -> list[Token]:
    tokens: list[Token] = []
    i = 0
    n = len(source)
    line = 1
    at_line_start = True

    def err(msg: str) -> TokenizeError:
        return TokenizeError(f"{path}:{line}: {msg}")

    while i < n:
        c = source[i]
        if c == "\n":
            line += 1
            i += 1
            at_line_start = True
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        if c == "\\" and source.startswith("\n", i + 1):
            line += 1
            i += 2
            continue
        if source.startswith("//", i):
            end = source.find("\n", i)
            i = n if end < 0 else end
            continue
        if source.startswith("/*", i):
            end = source.find("*/", i + 2)
            if end < 0:
                raise err("unterminated block comment")
            line += source.count("\n", i, end)
            i = end + 2
            continue
        if c == "#" and at_line_start:
            start, start_line = i, line
            in_line_comment = False
            while i < n:
                if source[i] == "\\" and source.startswith("\n", i + 1):
                    line += 1
                    i += 2
                    continue
                if source[i] == "\n":
                    break
                if in_line_comment:
                    i += 1
                    continue
                if source.startswith("//", i):
                    in_line_comment = True
                    i += 2
                    continue
                literal_end = _literal_at(source, i, [], path, line, err)
                if literal_end is not None:
                    i, line = literal_end
                    continue
                word = _IDENT.match(source, i) or _NUMBER.match(source, i)
                if word:
                    i = word.end()
                    continue
                if source.startswith("/*", i):
                    end = source.find("*/", i + 2)
                    if end < 0:
                        raise err("unterminated block comment")
                    line += source.count("\n", i, end)
                    i = end + 2
                    continue
                i += 1
            raw = source[start:i]
            tokens.append(Token("directive", _directive_text(raw, path, start_line), path, start_line, start, i))
            continue
        at_line_start = False

        literal_end = _literal_at(source, i, tokens, path, line, err)
        if literal_end is not None:
            i, line = literal_end
            continue
        if _IDENT_START.match(c):
            m = _IDENT.match(source, i)
            word = m.group(0)
            after = m.end()
            tokens.append(Token("ident", word, path, line, i, after))
            i = after
            continue
        if c.isdigit() or (c == "." and i + 1 < n and source[i + 1].isdigit()):
            m = _NUMBER.match(source, i)
            tokens.append(Token("number", m.group(0), path, line, i, m.end()))
            i = m.end()
            continue
        for p in _PUNCTUATORS:
            if source.startswith(p, i):
                tokens.append(Token("punct", p, path, line, i, i + len(p)))
                i += len(p)
                break
        else:
            raise err(f"unexpected character {c!r}")
    return tokens


def _literal_at(source, start, tokens, path, line, err):
    """Consume a string or character literal at a token boundary, if present."""
    word = _IDENT.match(source, start)
    prefix = word.group(0) if word else ""
    quote = word.end() if word else start
    if quote < len(source) and source[quote] == '"' and prefix in _STRING_PREFIXES | {""}:
        return _string(source, start, quote, prefix.endswith("R"), tokens, path, line, err)
    if quote < len(source) and source[quote] == "'" and prefix in {"", "u8", "u", "U", "L"}:
        return _char(source, start, quote, tokens, path, line, err)
    return None


def _string(source, start, quote, raw, tokens, path, line, err):
    """Append a (possibly prefixed / raw) string literal; return (end, line)."""
    if raw:
        open_paren = source.find("(", quote + 1)
        if open_paren < 0:
            raise err("malformed raw string literal")
        delim = source[quote + 1 : open_paren]
        closing = ")" + delim + '"'
        end = source.find(closing, open_paren + 1)
        if end < 0:
            raise err(f"unterminated raw string literal R\"{delim}(")
        end += len(closing)
    else:
        end = quote + 1
        while True:
            if end >= len(source) or source[end] == "\n":
                raise err("unterminated string literal")
            if source[end] == "\\":
                end += 2
                continue
            if source[end] == '"':
                end += 1
                break
            end += 1
    # User-defined literal suffix, e.g. "abc"sv.
    m = _IDENT.match(source, end)
    if m:
        end = m.end()
    text = source[start:end]
    tokens.append(Token("string", text, path, line, start, end))
    return end, line + text.count("\n")


def _char(source, start, quote, tokens, path, line, err):
    end = quote + 1
    while True:
        if end >= len(source) or source[end] == "\n":
            raise err("unterminated character literal")
        if source[end] == "\\":
            end += 2
            continue
        if source[end] == "'":
            end += 1
            break
        end += 1
    tokens.append(Token("char", source[start:end], path, line, start, end))
    return end, line


# --- Entity extraction --------------------------------------------------------

DEFINITION = "definition"
DECLARATION = "declaration"
DIRECTIVE = "directive"
INCLUDE = "include"


@dataclasses.dataclass
class Entity:
    kind: str
    tokens: list[Token]
    namespace: str
    normalized: tuple[str, ...] = ()
    name: str = ""
    dropped_specifiers: tuple[str, ...] = ()

    @property
    def location(self) -> str:
        first = self.tokens[0]
        return f"{first.path}:{first.line}"


_TYPE_KEYWORDS = {"class", "struct", "union", "enum"}
_FUNCTION_SPECIFIERS = {"inline", "static"}
_TYPE_WORDS = {
    "void", "bool", "char", "char8_t", "char16_t", "char32_t", "wchar_t",
    "short", "int", "long", "float", "double", "signed", "unsigned",
    "const", "volatile", "auto", "typename", "class", "struct", "enum", "union",
}
_TYPE_QUERIES = {"decltype", "sizeof", "alignof", "alignas", "typeid", "noexcept",
                 "__attribute__", "__declspec"}


def _skip_template_header(texts: list[str]) -> int:
    """Index just past leading `template <...>` groups."""
    i = 0
    while i < len(texts) and texts[i] == "template" and i + 1 < len(texts) and texts[i + 1] == "<":
        depth = 0
        i += 1
        while i < len(texts):
            if texts[i] == "<":
                depth += 1
            elif texts[i] == ">":
                depth -= 1
            elif texts[i] == ">>":
                depth -= 2
            i += 1
            if depth <= 0:
                break
    return i


def _closing_delimiter(texts: list[str], start: int, opening: str, closing: str) -> int:
    depth = 0
    index = start
    while index < len(texts):
        token = texts[index]
        if opening == "<" and token in {"(", "[", "{"}:
            nested_closing = {"(": ")", "[": "]", "{": "}"}[token]
            index = _closing_delimiter(texts, index, token, nested_closing) + 1
            continue
        if token == opening:
            depth += 1
        elif token == closing:
            depth -= 1
        elif closing == ">" and token == ">>":
            depth -= 2
        if depth <= 0:
            return index
        index += 1
    raise TokenizeError(f"unclosed {opening!r} in declaration")


def _declarator_parentheses(texts: list[str]) -> tuple[int, int] | None:
    """Find the declarator's parentheses, skipping type queries, templates and
    array/attribute brackets. Both functions and direct initializers use them."""
    index = _skip_template_header(texts)
    while index < len(texts):
        token = texts[index]
        previous = texts[index - 1] if index else ""
        if token in {"{", ";"} or (token == "=" and previous != "operator"):
            return None
        if token in _TYPE_QUERIES and texts[index + 1:index + 2] == ["("]:
            index = _closing_delimiter(texts, index + 1, "(", ")") + 1
            continue
        if token == "[":
            index = _closing_delimiter(texts, index, "[", "]") + 1
            continue
        if token == "<" and previous != "operator":
            index = _closing_delimiter(texts, index, "<", ">") + 1
            if texts[index:index + 1] == ["("]:
                end = _closing_delimiter(texts, index, "(", ")")
                if _clearly_function_parameters(texts[index + 1:end]):
                    return index, end
            continue
        if token == "(" and previous not in _TYPE_WORDS:
            if _IDENT.fullmatch(previous) or "operator" in texts[:index]:
                return index, _closing_delimiter(texts, index, "(", ")")
        index += 1
    return None


def _needs_semicolon(texts: list[str]) -> bool:
    """After a top-level `}` closes: does the entity continue to a `;`?

    True for class/struct/union/enum definitions and for variables whose
    initializer contains braces (brace-init, lambdas). False for functions.
    Only tokens outside parentheses count, so default arguments (`int x = 0`)
    and parameter types (`struct tm*`) do not turn a function into a variable.
    """
    clause = _declarator_parentheses(texts)
    if clause is None:
        return True
    tail = texts[clause[1] + 1:]
    return not tail or tail[0] in {",", "=", ";"} or "{" not in tail


def _clearly_function_parameters(texts: list[str]) -> bool:
    """Recognize an unambiguous parameter list. A single unknown identifier
    could name a type or an initializer value, so its changes stay gated."""
    if not texts or texts[0] == "...":
        return True
    if texts[0] in _TYPE_WORDS:
        return len(texts) == 1 or texts[1] not in {"(", "{"}
    # A type followed directly by a parameter name cannot be an expression.
    return len(texts) > 1 and bool(_IDENT.fullmatch(texts[0]) and _IDENT.fullmatch(texts[1]))


def extract_entities(tokens: list[Token]) -> list[Entity]:
    entities: list[Entity] = []
    stack: list[str] = []  # namespace path components; "" marks extern "C" {
    current: list[Token] = []
    depth = 0
    parens = 0  # parentheses open in the current entity (default arguments)
    closed_body = False
    i = 0

    def ns() -> str:
        return "::".join(s for s in stack if not s.startswith("\0")) or "(global)"

    def flush(kind: str) -> None:
        nonlocal current, closed_body, parens
        if current:
            entities.append(Entity(kind, current, ns()))
        current = []
        closed_body = False
        parens = 0

    while i < len(tokens):
        tok = tokens[i]
        if closed_body:
            # A function definition ended at its closing brace; the next token
            # starts a new entity (a stray `;` is kept attached).
            if tok.text == ";":
                current.append(tok)
                i += 1
            flush(DEFINITION)
            continue
        if tok.kind == "directive":
            if depth == 0 and not current:
                kind = INCLUDE if tok.text.startswith(("#include", "#pragma once")) else DIRECTIVE
                entities.append(Entity(kind, [tok], ns()))
            else:
                current.append(tok)
            i += 1
            continue

        if depth == 0:
            texts = [t.text for t in current]
            if tok.text == "{" and _opens_scope(texts):
                stack.extend(_scope_names(texts))
                current = []
                i += 1
                continue
            if tok.text == "}" and not current:
                if not stack:
                    raise TokenizeError(f"{tok.path}:{tok.line}: unbalanced '}}' at namespace scope")
                # Pop one `namespace a::b::c {` group (recorded as a marker).
                while stack:
                    if stack.pop().startswith("\0"):
                        break
                i += 1
                continue

        current.append(tok)
        if tok.text == "(":
            parens += 1
        elif tok.text == ")":
            parens -= 1
        elif tok.text == "{":
            depth += 1
        elif tok.text == "}":
            depth -= 1
            if depth < 0:
                raise TokenizeError(f"{tok.path}:{tok.line}: unbalanced '}}'")
            if depth == 0 and parens == 0 and not _needs_semicolon([t.text for t in current]):
                # A brace group inside a constructor's member-initializer list
                # (`: a_{1}, b_{2} {`) is followed by `,` or the body's `{`.
                nxt = _next_code_token(tokens, i + 1)
                closed_body = nxt is None or nxt.text not in {",", "{"}
        elif tok.text == ";" and depth == 0:
            flush(_semicolon_entity_kind([t.text for t in current]))
        i += 1

    if closed_body:
        flush(DEFINITION)
    if current:
        raise TokenizeError(f"{current[0].path}:{current[0].line}: unterminated declaration")
    if depth != 0 or stack:
        raise TokenizeError("unbalanced braces or namespaces at end of input")
    return entities


def _semicolon_entity_kind(texts: list[str]) -> str:
    """Kind of a namespace-scope entity that ends with `;`.

    Declarations: function prototypes, forward class declarations, `extern`
    declarations, using-directives and using-declarations. Everything else --
    including a variable without an initializer (`static char k;` is a
    definition) and alias declarations (`using X = Y;`) -- is a definition.
    """
    if "{" in texts:
        return DEFINITION
    head = texts[_skip_template_header(texts):]
    if head[:1] == ["using"] and "=" not in head:
        return DECLARATION
    if "=" in head and not (head.index("=") > 0 and head[head.index("=") - 1] == "operator"):
        # `= default;` / `= delete;` keep a prototype a declaration.
        tail = head[head.index("=") + 1 :]
        return DECLARATION if tail[:1] in (["default"], ["delete"]) and "(" in head else DEFINITION
    if head[:1] in (["extern"], ["friend"]):
        return DECLARATION
    if head and head[0] in _TYPE_KEYWORDS and len(head) <= 4:
        return DECLARATION  # class X; / enum class X : int;
    clause = _declarator_parentheses(head)
    if clause is not None and _clearly_function_parameters(head[clause[0] + 1:clause[1]]):
        return DECLARATION
    return DEFINITION


def _next_code_token(tokens: list[Token], start: int) -> Token | None:
    for t in tokens[start:]:
        if t.kind != "directive":
            return t
    return None


def _opens_scope(texts: list[str]) -> bool:
    if texts[:1] == ["namespace"] or texts[:2] == ["inline", "namespace"]:
        return "=" not in texts
    return texts[:1] == ["extern"] and len(texts) == 2 and texts[1].startswith('"')


def _scope_names(texts: list[str]) -> list[str]:
    """Stack entries for `namespace a::b {` (a marker first, popped as a group)."""
    if texts[0] == "extern":
        return ["\0", 'extern "C"']
    names = [t for t in texts if t not in {"namespace", "inline", "::"}]
    return ["\0"] + (names or ["(anonymous)"])


# --- Normalization and naming -------------------------------------------------


def normalize(entity: Entity) -> None:
    texts = [t.text for t in entity.tokens]
    if entity.kind == DEFINITION and "{" in texts and not _needs_semicolon(texts):
        # Function definition: ignore leading inline/static (see module doc).
        start = _skip_template_header(texts)
        dropped = []
        while start < len(texts) and texts[start] in _FUNCTION_SPECIFIERS:
            dropped.append(texts.pop(start))
        entity.dropped_specifiers = tuple(dropped)
    entity.normalized = tuple(texts)
    entity.name = _entity_name(texts) if entity.kind in (DEFINITION, DECLARATION) else texts[0]


def _entity_name(texts: list[str]) -> str:
    head = texts[_skip_template_header(texts):]
    stop = len(head)
    for idx, t in enumerate(head):
        if t in {"{", ";"} or (t == "=" and (idx == 0 or head[idx - 1] != "operator")):
            stop = idx
            break
    prefix = head[:stop]
    for idx, t in enumerate(prefix):
        if t in _TYPE_KEYWORDS:
            rest = [x for x in prefix[idx + 1 :] if x not in {"class", "struct", "final"}]
            names = [x for x in rest if _IDENT.fullmatch(x)]
            return f"{t} {names[0]}" if names else t
        if t == "(":
            return _qualified_before(prefix, idx) + "()"
    if prefix[:1] == ["using"] and len(prefix) > 1:
        return "using " + prefix[1]
    names = [x for x in prefix if _IDENT.fullmatch(x)]
    return names[-1] if names else " ".join(prefix[:3])


def _qualified_before(texts: list[str], idx: int) -> str:
    parts: list[str] = []
    j = idx - 1
    if j >= 0 and texts[j] not in {"::"} and not _IDENT.fullmatch(texts[j]):
        # operator overloads: `operator==(`, `operator()(`
        k = j
        while k >= 0 and texts[k] != "operator":
            k -= 1
        if k >= 0:
            return _qualified_before(texts, k) + "operator" + "".join(texts[k + 1 : idx])
    while j >= 0 and (_IDENT.fullmatch(texts[j]) or texts[j] in {"::", "~"}):
        parts.append(texts[j])
        j -= 1
        if parts[-1] not in {"::", "~"} and j >= 0 and texts[j] not in {"::", "~"}:
            break
    return "".join(reversed(parts))


# --- Sources ------------------------------------------------------------------


def _git(*args: str) -> str:
    result = subprocess.run(["git", *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def read_side(ref: str | None, paths: list[str]) -> dict[str, str]:
    """{path: source} for files that exist on this side (ref None = worktree)."""
    out = {}
    for path in paths:
        if ref is None:
            p = Path(path)
            if p.is_file():
                out[path] = p.read_text(encoding="utf-8")
        else:
            result = subprocess.run(
                ["git", "show", f"{ref}:{path}"], capture_output=True, text=True, encoding="utf-8"
            )
            if result.returncode == 0:
                out[path] = result.stdout
    return out


def changed_cpp_files(base: str, head: str | None) -> list[str]:
    args = ["diff", "--name-only", "--no-renames", base]
    if head is not None:
        args.append(head)
    names = _git(*args, "--").splitlines()
    if head is None:
        names += _git("ls-files", "--others", "--exclude-standard").splitlines()
    return sorted({n for n in names if n.endswith(CPP_SUFFIXES)})


def member_function_parts(tokens: list[Token]) -> tuple[int, int, int, int]:
    """Return name start, parameter delimiters and body start for a method.

    This deliberately rejects unsupported declarators instead of weakening the
    definition gate. Constructor brace initializers precede the final body.
    """
    texts = [token.text for token in tokens]
    clause = _declarator_parentheses(texts)
    if clause is None or "friend" in texts[:clause[0]] or "operator" in texts[:clause[0]]:
        raise TokenizeError("member audit requires an ordinary member or constructor declarator")
    opening, closing = clause
    name = opening - 1
    if name > 0 and texts[name - 1] == "~":
        name -= 1
    depth = 0
    for index in range(len(texts) - 1, -1, -1):
        if texts[index] == "}":
            depth += 1
        elif texts[index] == "{":
            depth -= 1
            if depth == 0:
                if index <= closing or "try" in texts[closing + 1:index]:
                    break
                return name, opening, closing, index
    raise TokenizeError("member audit could not identify a function body")


def member_declaration_tokens(tokens: list[Token]) -> list[Token]:
    """Keep the complete declaration contract, excluding body/ctor initializers."""
    _, _, closing, body = member_function_parts(tokens)
    end = body
    index = closing + 1
    while index < body:
        if tokens[index].text == "(":
            index = _closing_delimiter([token.text for token in tokens], index, "(", ")") + 1
            continue
        if tokens[index].text == ":":
            end = index
            break
        index += 1
    return tokens[:end] + [dataclasses.replace(tokens[body], text=";")]


def _without_parameter_defaults(tokens: list[Token]) -> list[Token]:
    result: list[Token] = []
    stack: list[str] = []
    skipping = False
    pairs = {")": "(", "]": "[", "}": "{", ">": "<"}
    for token in tokens:
        text = token.text
        if not stack and text == "=":
            skipping = True
            continue
        if not stack and text == ",":
            skipping = False
        if not skipping:
            result.append(token)
        if text in {"(", "[", "{"} or (skipping and text == "<"):
            stack.append(text)
        elif text in pairs and stack and stack[-1] == pairs[text]:
            stack.pop()
        elif text == ">>" and stack[-2:] == ["<", "<"]:
            del stack[-2:]
    if stack:
        raise TokenizeError("member audit cannot resolve this parameter default")
    return result


def _member_implementation(entity: Entity, owner: str) -> Entity:
    tokens = entity.tokens
    name, opening, closing, body = member_function_parts(tokens)
    prefix = [token for token in tokens[:name]
              if token.text not in {"virtual", "static", "explicit"}]
    qualifier = [dataclasses.replace(tokens[name], text=text) for text in (owner, "::")]
    parameters = _without_parameter_defaults(tokens[opening + 1:closing])
    suffix = [token for token in tokens[closing + 1:body] if token.text not in {"override", "final"}]
    canonical = (prefix + qualifier + tokens[name:opening + 1] + parameters
                 + tokens[closing:closing + 1] + suffix + tokens[body:])
    return Entity(DEFINITION, canonical, entity.namespace)


def _decompose_class(entity: Entity, owner: str) -> list[Entity]:
    tokens = entity.tokens
    opening = next(index for index, token in enumerate(tokens) if token.text == "{")
    closing = max(index for index, token in enumerate(tokens) if token.text == "}")
    body = tokens[opening + 1:closing]
    clean: list[Token] = []
    depth = 0
    index = 0
    while index < len(body):
        text = body[index].text
        if (depth == 0 and text in {"public", "protected", "private"}
                and index + 1 < len(body) and body[index + 1].text == ":"):
            index += 2
            continue
        clean.append(body[index])
        depth += (text == "{") - (text == "}")
        index += 1
    positions = {id(token): index for index, token in enumerate(tokens)}
    replacements = []
    implementations = []
    for member in extract_entities(clean):
        texts = [token.text for token in member.tokens]
        if member.kind != DEFINITION or _needs_semicolon(texts):
            continue
        replacements.append((positions[id(member.tokens[0])],
                             positions[id(member.tokens[-1])] + 1,
                             member_declaration_tokens(member.tokens)))
        implementations.append(_member_implementation(member, owner))
    skeleton = list(tokens)
    for start, end, declaration in reversed(replacements):
        skeleton[start:end] = declaration
    return [dataclasses.replace(entity, tokens=skeleton), *implementations]


def entities_of(sources: dict[str, str], member_classes: tuple[str, ...] = ()) -> list[Entity]:
    out = []
    for path, text in sorted(sources.items()):
        for entity in extract_entities(tokenize(text, path)):
            texts = [token.text for token in entity.tokens]
            owner = texts[1] if len(texts) > 1 and texts[0] in {"class", "struct"} else ""
            parts = (_decompose_class(entity, owner)
                     if owner in member_classes and entity.kind == DEFINITION and "{" in texts
                     else [entity])
            for part in parts:
                normalize(part)
                out.append(part)
    return out


# --- Comparison ---------------------------------------------------------------


@dataclasses.dataclass
class Report:
    old: list[Entity]
    new: list[Entity]
    matched: list[tuple[Entity, Entity]]
    removed: list[Entity]
    added: list[Entity]

    def of_kind(self, items: list[Entity], kinds: set[str]) -> list[Entity]:
        return [e for e in items if e.kind in kinds]

    @property
    def definition_differences(self) -> int:
        return len(self.of_kind(self.removed, {DEFINITION})) + len(self.of_kind(self.added, {DEFINITION}))


def compare(old: list[Entity], new: list[Entity]) -> Report:
    """Multiset match on (kind, normalized tokens); same-file partners first so
    that duplicated entities (e.g. `#include <string>`) are attributed sensibly."""
    pool: dict[tuple, list[Entity]] = collections.defaultdict(list)
    for e in new:
        pool[(e.kind, e.normalized)].append(e)
    matched, unmatched = [], []
    for same_file_only in (True, False):
        for e in (old if same_file_only else unmatched_old):
            bucket = pool.get((e.kind, e.normalized))
            partner = None
            if bucket:
                if same_file_only:
                    partner = next((b for b in bucket if b.tokens[0].path == e.tokens[0].path), None)
                else:
                    partner = bucket[0]
            if partner is not None:
                bucket.remove(partner)
                matched.append((e, partner))
            else:
                unmatched.append(e)
        if same_file_only:
            unmatched_old, unmatched = unmatched, []
    removed = unmatched
    added = [e for bucket in pool.values() for e in bucket]
    added.sort(key=lambda e: (e.tokens[0].path, e.tokens[0].line))
    return Report(old, new, matched, removed, added)


def _render(entity: Entity) -> list[str]:
    """Token stream broken into lines at ; { } for a readable diff."""
    lines, cur = [], []
    for t in entity.normalized:
        cur.append(t)
        if t in {";", "{", "}"} or t.startswith("#"):
            lines.append(" ".join(cur))
            cur = []
    if cur:
        lines.append(" ".join(cur))
    return lines


def print_report(report: Report, args, out=sys.stdout) -> None:
    p = lambda *a: print(*a, file=out)  # noqa: E731
    gated = report.definition_differences
    p(f"audit_move: base={args.base} head={args.head or '(working tree)'}")
    p(f"files: {', '.join(args.files) if args.files else '(none)'}")
    old_defs = len(report.of_kind(report.old, {DEFINITION}))
    new_defs = len(report.of_kind(report.new, {DEFINITION}))
    moved = sum(1 for a, b in report.matched if a.location.split(":")[0] != b.location.split(":")[0])
    p(f"definitions: base {old_defs}, head {new_defs}, identical "
      f"{sum(1 for a, _ in report.matched if a.kind == DEFINITION)} ({moved} entities changed file)")

    removed_defs = report.of_kind(report.removed, {DEFINITION})
    added_defs = report.of_kind(report.added, {DEFINITION})
    if gated:
        p("")
        p(f"DEFINITION DIFFERENCES: {gated}")
        added_by_name = collections.defaultdict(list)
        for e in added_defs:
            added_by_name[e.name].append(e)
        for e in removed_defs:
            partner = added_by_name[e.name].pop(0) if added_by_name.get(e.name) else None
            if partner is None:
                p(f"  - removed  {e.name}  [{e.namespace}]  {e.location}")
                continue
            p(f"  ~ changed  {e.name}  {e.location} -> {partner.location}")
            diff = difflib.unified_diff(_render(e), _render(partner), "base", "head", lineterm="", n=1)
            for line in list(diff)[2:]:
                p(f"      {line}")
        for name in added_by_name:
            for e in added_by_name[name]:
                p(f"  + added    {e.name}  [{e.namespace}]  {e.location}")

    info = []
    for e in report.of_kind(report.removed, {DECLARATION, DIRECTIVE, INCLUDE}):
        info.append(f"  - {e.kind:<11} {' '.join(e.normalized)[:100]}  ({e.location})")
    for e in report.of_kind(report.added, {DECLARATION, DIRECTIVE, INCLUDE}):
        info.append(f"  + {e.kind:<11} {' '.join(e.normalized)[:100]}  ({e.location})")
    ns_changes = collections.Counter(
        (a.namespace, b.namespace) for a, b in report.matched if a.namespace != b.namespace
    )
    for (src, dst), count in sorted(ns_changes.items()):
        info.append(f"  ~ namespace   {src} -> {dst}: {count} entities")
    spec_changes = [
        (a, b) for a, b in report.matched if a.dropped_specifiers != b.dropped_specifiers
    ]
    for a, b in spec_changes:
        info.append(
            f"  ~ specifiers  {a.name}: {' '.join(a.dropped_specifiers) or '(none)'} -> "
            f"{' '.join(b.dropped_specifiers) or '(none)'}  ({b.location})"
        )
    if info:
        p("")
        p(f"review (not gated): {len(info)}")
        shown = info if args.verbose else info[:40]
        for line in shown:
            p(line)
        if len(shown) < len(info):
            p(f"  ... {len(info) - len(shown)} more (use --verbose)")

    p("")
    p(f"RESULT: {gated} definition difference{'s' if gated != 1 else ''}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--base", required=True, help="git ref of the code before the move")
    parser.add_argument("--head", help="git ref after the move (default: working tree)")
    parser.add_argument("--verbose", action="store_true", help="list every review item")
    parser.add_argument("--class-members", action="append", default=[], metavar="CLASS",
                        help="audit inline-to-out-of-line moves for the named class (repeatable)")
    parser.add_argument("files", nargs="*",
                        help="files compared on both sides (default: C++ files changed since --base)")
    args = parser.parse_args(argv)
    try:
        if not args.files:
            args.files = changed_cpp_files(args.base, args.head)
        report = compare(
            entities_of(read_side(args.base, args.files), tuple(args.class_members)),
            entities_of(read_side(args.head, args.files), tuple(args.class_members)),
        )
    except (RuntimeError, TokenizeError) as exc:
        print(f"audit_move: error: {exc}", file=sys.stderr)
        return 2
    print_report(report, args)
    return 1 if report.definition_differences else 0


if __name__ == "__main__":
    sys.exit(main())
