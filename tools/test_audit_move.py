"""Self-tests for tools/audit_move.py (stdlib + pytest; no V8 needed).

Run: python -m pytest tools/test_audit_move.py -q
Not collected by CI's `pytest test` run, so it does not change runtime test
counts.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audit_move as am  # noqa: E402


INLINE_MEMBERS = r'''
namespace iv8::browser {
class Doc : public Base {
public:
    explicit Doc(int value = 3) : value_{value}, other_(5) { value_ += other_; }
    ~Doc() noexcept override { close(); }
    int get(int delta = 0) const noexcept { return value_ + delta; }
    static int scale(int value) { return value * 2; }
    virtual const char* label() const { return "label"; }
private:
    int value_ = 7;
    int other_;
};
}
'''
OUTLINE_MEMBERS = r'''
namespace iv8::browser {
class Doc : public Base {
public:
    explicit Doc(int value = 3);
    ~Doc() noexcept override;
    int get(int delta = 0) const noexcept;
    static int scale(int value);
    virtual const char* label() const;
private:
    int value_ = 7;
    int other_;
};
Doc::Doc(int value) : value_{value}, other_(5) { value_ += other_; }
Doc::~Doc() noexcept { close(); }
int Doc::get(int delta) const noexcept { return value_ + delta; }
int Doc::scale(int value) { return value * 2; }
const char* Doc::label() const { return "label"; }
}
'''


def member_audit(old=INLINE_MEMBERS, new=OUTLINE_MEMBERS):
    return am.compare(am.entities_of({'a.cpp': old}, ('Doc',)),
                      am.entities_of({'b.cpp': new}, ('Doc',)))


def test_member_move_preserves_constructor_destructor_defaults_and_contract():
    report = member_audit()
    assert report.definition_differences == 0
    assert len([item for item in report.old if item.kind == am.DEFINITION]) == 6
    assert all(old.namespace == new.namespace for old, new in report.matched)


@pytest.mark.parametrize(('before', 'after'), [
    ('value_ + delta', 'value_ - delta'),
    ('value * 2', 'value * 3'),
    ('"label"', '"changed"'),
    ('value_{value}', 'value_{9}'),
    ('other_(5)', 'other_(6)'),
    ('close();', 'other();'),
    ('int value = 3', 'int value = 4'),
    ('int value_ = 7', 'int value_ = 8'),
    ('int other_;', 'long other_;'),
    ('public:', 'protected:'),
    ('static int scale', 'int scale'),
    ('virtual const char* label', 'const char* label'),
    ('explicit Doc', 'Doc'),
    ('noexcept override;', 'override;'),
    ('const noexcept;', 'noexcept;'),
    ('public Base', 'public Different'),
    ('delta = 0', 'delta = 1'),
])
def test_member_audit_gates_body_and_class_contract_changes(before, after):
    assert before in OUTLINE_MEMBERS
    assert member_audit(new=OUTLINE_MEMBERS.replace(before, after, 1)).definition_differences > 0


def test_member_audit_gates_missing_implementation():
    text = OUTLINE_MEMBERS.replace('int Doc::scale(int value) { return value * 2; }', '')
    assert member_audit(new=text).definition_differences > 0


def test_member_audit_gates_duplicate_implementation():
    assert member_audit(new=OUTLINE_MEMBERS + '\nint Doc::scale(int value) { return value * 2; }').definition_differences > 0


def test_member_audit_is_opt_in():
    assert audit({'a.cpp': INLINE_MEMBERS}, {'b.cpp': OUTLINE_MEMBERS}).definition_differences > 0


def test_member_audit_preserves_nested_data_definitions_and_lambda_initializers():
    field = 'struct Nested { int count = 2; }; int value_ = [] { return 7; }();'
    old = INLINE_MEMBERS.replace('int value_ = 7;', field)
    new = OUTLINE_MEMBERS.replace('int value_ = 7;', field)
    assert member_audit(old, new).definition_differences == 0
    assert member_audit(old, new.replace('count = 2', 'count = 3')).definition_differences > 0


def test_member_audit_preserves_nested_parameter_defaults():
    old = 'class Doc { public: int f(int n = helper(1, 2), int m = 3) { return n + m; } };'
    new = 'class Doc { public: int f(int n = helper(1, 2), int m = 3); }; int Doc::f(int n, int m) { return n + m; }'
    assert member_audit(old, new).definition_differences == 0


@pytest.mark.parametrize('method', ['int operator()() { return 1; }', 'friend int helper() { return 1; }'])
def test_member_audit_rejects_unsupported_declarators(method):
    with pytest.raises(am.TokenizeError):
        am.entities_of({'a.cpp': 'class Doc { ' + method + ' };'}, ('Doc',))


def test_token_source_offsets_cover_literals_comments_and_braces():
    source = 'int f(/* skip */ int x) { return x + sizeof(R"x(})x"); }'
    for token in am.tokenize(source, 'test.cpp'):
        assert source[token.start:token.end] == token.text


def test_member_audit_cli_gates_an_implementation_change(monkeypatch):
    sources = {'before': INLINE_MEMBERS, 'after': OUTLINE_MEMBERS}
    monkeypatch.setattr(am, 'read_side', lambda ref, paths: {'sample.cpp': sources[ref]})
    arguments = ['--base', 'before', '--head', 'after', '--class-members', 'Doc', 'sample.cpp']
    assert am.main(arguments) == 0
    sources['after'] = OUTLINE_MEMBERS.replace('value * 2', 'value * 3')
    assert am.main(arguments) == 1


def audit(old: dict[str, str], new: dict[str, str]) -> am.Report:
    return am.compare(am.entities_of(old), am.entities_of(new))


def kinds(source: str) -> list[tuple[str, str]]:
    return [(e.kind, e.name) for e in am.entities_of({"f.cpp": source})]


ORIGINAL = r'''
#include "iv8/page_state.h"
namespace iv8 {
namespace {
constexpr char kToken = 0;
static char kStatic;
int helper(int x = 0) { return x + 1; }
struct Box { int v{0}; };
class Host {
public:
    Host() : a_{1}, b_{2} { if (a_) { b_ = 3; } }
    Host& operator=(const Host&) = default;
private:
    int a_; int b_;
};
constexpr char kScript[] = R"JS(
  function f() { return "}"; } // not a comment for C++
)JS";
bool rng(unsigned char* out) {
#if defined(_WIN32)
    return out != nullptr;
#else
    return out == nullptr;
#endif
}
auto lambda = [](int v) { return v * 2; };
}  // namespace
}  // namespace iv8
'''


def test_identical_sources_have_no_differences():
    report = audit({"a.cpp": ORIGINAL}, {"a.cpp": ORIGINAL})
    assert report.definition_differences == 0
    assert not report.removed and not report.added


def test_layout_and_comments_are_ignored():
    reformatted = ORIGINAL.replace("return x + 1;", "return x+1; /* tidy */").replace(
        "struct Box { int v{0}; };", "struct Box {\n    int v{0};  /* zero */\n};")
    assert audit({"a.cpp": ORIGINAL}, {"a.cpp": reformatted}).definition_differences == 0


def test_pure_move_across_files_and_namespace_syntax():
    moved_out = ORIGINAL.replace("int helper(int x = 0) { return x + 1; }\n", "")
    new_file = "namespace iv8::browser {\nint helper(int x = 0) { return x + 1; }\n}\n"
    report = audit({"a.cpp": ORIGINAL}, {"a.cpp": moved_out, "b.cpp": new_file})
    assert report.definition_differences == 0
    (pair,) = [(a, b) for a, b in report.matched if a.name == "helper()"]
    assert pair[1].tokens[0].path == "b.cpp"
    assert (pair[0].namespace, pair[1].namespace) == ("iv8::(anonymous)", "iv8::browser")


def test_body_edit_is_reported_as_changed():
    edited = ORIGINAL.replace("return x + 1;", "return x + 2;")
    report = audit({"a.cpp": ORIGINAL}, {"a.cpp": edited})
    assert report.definition_differences == 2
    assert [e.name for e in report.removed] == ["helper()"]
    assert [e.name for e in report.added] == ["helper()"]


def test_inline_on_moved_function_is_normalized_but_reported():
    with_inline = ORIGINAL.replace("int helper(", "inline int helper(")
    report = audit({"a.cpp": ORIGINAL}, {"a.cpp": with_inline})
    assert report.definition_differences == 0
    (pair,) = [(a, b) for a, b in report.matched if a.name == "helper()"]
    assert pair[1].dropped_specifiers == ("inline",)


@pytest.mark.parametrize(
    "before, after",
    [
        ("constexpr char kToken = 0;", "inline constexpr char kToken = 0;"),
        ("static char kStatic;", "inline char kStatic;"),
        ("static char kStatic;", "char kStatic;"),
    ],
)
def test_variable_linkage_changes_are_never_normalized(before, after):
    report = audit({"a.cpp": ORIGINAL}, {"a.cpp": ORIGINAL.replace(before, after)})
    assert report.definition_differences == 2


def test_raw_string_contents_are_one_token():
    tokens = am.tokenize(ORIGINAL, "a.cpp")
    raw = [t for t in tokens if t.kind == "string" and t.text.startswith("R")]
    assert len(raw) == 1 and 'return "}";' in raw[0].text
    edited = ORIGINAL.replace('return "}";', 'return "{";')
    assert audit({"a.cpp": ORIGINAL}, {"a.cpp": edited}).definition_differences == 2


def test_entity_boundaries():
    assert kinds(ORIGINAL) == [
        ("include", '#include "iv8/page_state.h"'),
        ("definition", "kToken"),
        ("definition", "kStatic"),
        ("definition", "helper()"),
        ("definition", "struct Box"),
        ("definition", "class Host"),
        ("definition", "kScript"),
        ("definition", "rng()"),
        ("definition", "lambda"),
    ]


def test_constructor_initializer_braces_and_defaults():
    source = (
        "Foo::Foo() : a_{1}, b_{} { run(); }\n"
        "int after(int x = 1, int y = {}) { return x; }\n"
        "Foo& Foo::operator=(const Foo&) { return *this; }\n"
        "void last();\n"
    )
    assert kinds(source) == [
        ("definition", "Foo::Foo()"),
        ("definition", "after()"),
        ("definition", "Foo::operator=()"),
        ("declaration", "last()"),
    ]


def test_declarations_versus_definitions():
    source = (
        "class Fwd;\n"
        "extern \"C\" int c_api(void);\n"
        "using namespace std;\n"
        "using Alias = int;\n"
        "int proto(int);\n"
        "static char uninitialized;\n"
        "namespace py = pybind11;\n"
    )
    assert [k for k, _ in kinds(source)] == [
        "declaration", "declaration", "declaration", "definition",
        "declaration", "definition", "definition",
    ]


def test_directives_inside_and_between_functions():
    source = "void a() {\n#if X\n  f();\n#endif\n}\n#if Y\nvoid b() {}\n#endif\n"
    ents = am.entities_of({"f.cpp": source})
    assert [(e.kind, e.name) for e in ents] == [
        ("definition", "a()"),
        ("directive", "#if Y"),
        ("definition", "b()"),
        ("directive", "#endif"),
    ]
    assert "#if X" in ents[0].normalized


def test_extern_c_block_and_nested_namespaces():
    source = 'extern "C" {\nint c_fn(void);\n}\nnamespace a { namespace b { int v = 1; } }\n'
    ents = am.entities_of({"f.cpp": source})
    assert [(e.name, e.namespace) for e in ents] == [
        ("c_fn()", 'extern "C"'),
        ("v", "a::b"),
    ]


def test_tokenize_errors_are_reported():
    with pytest.raises(am.TokenizeError):
        am.extract_entities(am.tokenize("namespace a { int v = 1;\n", "bad.cpp"))
    with pytest.raises(am.TokenizeError):
        am.tokenize('const char* s = "unterminated;\n', "bad.cpp")


def test_repository_cpp_sources_parse_and_self_match():
    root = Path(__file__).resolve().parent.parent
    files = sorted(
        str(p.relative_to(root))
        for pattern in ("src/**/*.cpp", "src/**/*.h", "include/**/*.h")
        for p in root.glob(pattern)
    )
    assert any(f.endswith("page_state.cpp") for f in files)
    sources = {f: (root / f).read_text(encoding="utf-8") for f in files}
    report = audit(sources, sources)
    assert report.definition_differences == 0
    assert sum(1 for e in report.old if e.kind == am.DEFINITION) > 400
