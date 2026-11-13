"""Regression checks for variable definitions and preprocessor string literals."""

import importlib.util
from pathlib import Path
import subprocess
import sys

import pytest


tool_path = Path(__file__).resolve().parents[1] / "tools" / "audit_move.py"
spec = importlib.util.spec_from_file_location("iv8_audit_move_regressions", tool_path)
audit_move = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = audit_move
spec.loader.exec_module(audit_move)


@pytest.mark.parametrize("declaration", [
    "constexpr char token{0};",
    "constexpr char token[]{0};",
    "decltype(0) token{0};",
    "std::array<decltype(0), 1> token{0};",
    "std::array<int, (1 < 2)> token{0};",
    "int token(0);",
    "int token(seed);",
    "Box token(make_value(0));",
    "Box token({0});",
    "int first(1), second{2};",
    "int (*token)(){nullptr};",
    "std::array<int, 1> (token){0};",
])
def test_variable_specifiers_are_preserved(declaration):
    old = audit_move.entities_of({"before.cpp": "static " + declaration})
    new = audit_move.entities_of({"after.cpp": "inline " + declaration})
    assert len(old) == len(new) == 1
    assert old[0].kind == new[0].kind == audit_move.DEFINITION
    assert old[0].dropped_specifiers == new[0].dropped_specifiers == ()
    assert audit_move.compare(old, new).definition_differences == 2


@pytest.mark.parametrize("before,after", [
    ("int value(1);", "int value(2);"),
    ("Box value(seed);", "Box value(other_seed);"),
    ("Box value(make_value(1));", "Box value(make_value(2));"),
    ('std::string value("a  b");', 'std::string value("a b");'),
    ("extern int value = 1;", "extern int value = 2;"),
])
def test_variable_initializer_changes_are_gated(before, after):
    old = audit_move.entities_of({"before.cpp": before})
    new = audit_move.entities_of({"after.cpp": after})
    assert old[0].kind == new[0].kind == audit_move.DEFINITION
    assert audit_move.compare(old, new).definition_differences == 2


@pytest.mark.parametrize("source", [
    "int helper(int value = 0) { return value; }",
    "decltype(0) helper() { return 0; }",
    "Box::Box() : first{1}, second{} {}",
])
def test_function_moves_still_normalize_inline(source):
    old = audit_move.entities_of({"before.cpp": source})
    new = audit_move.entities_of({"after.cpp": "inline " + source})
    assert len(old) == len(new) == 1
    assert new[0].dropped_specifiers == ("inline",)
    assert audit_move.compare(old, new).definition_differences == 0


def test_specialized_function_moves_still_normalize_inline():
    source = "template<> int helper<int>() { return 0; }"
    old = audit_move.entities_of({"before.cpp": source})
    new = audit_move.entities_of({"after.cpp": source.replace("int helper", "inline int helper")})
    assert len(old) == len(new) == 1
    assert new[0].dropped_specifiers == ("inline",)
    assert audit_move.compare(old, new).definition_differences == 0


def test_brace_initialized_variables_keep_entity_boundaries():
    source = "static int first{1}, second{2};\ndecltype(0) third{};\nvoid after() {}\n"
    entities = audit_move.entities_of({"source.cpp": source})
    assert len(entities) == 3
    assert all(entity.kind == audit_move.DEFINITION for entity in entities)
    assert entities[0].normalized[:2] == ("static", "int")


@pytest.mark.parametrize("before,after", [
    ('"a  b"', '"a b"'),
    ('"a\tb"', '"a b"'),
    ("'  '", "' '"),
    ('u8"a  b"', 'u8"a b"'),
    ('R"JS(a "  b)JS"', 'R"JS(a " b)JS"'),
])
def test_macro_literal_contents_are_preserved(before, after):
    template = "const char* text() {\n#define TEXT %s\nreturn TEXT;\n}\n"
    old = audit_move.entities_of({"before.cpp": template % before})
    new = audit_move.entities_of({"after.cpp": template % after})
    assert audit_move.compare(old, new).definition_differences == 2


@pytest.mark.parametrize("literal", [
    '"/*"', '"//"', "'/'", '"escaped \\\" quote"',
    'R"JS(a /* comment marker */ " quote\nsecond line)JS"',
])
def test_macro_literals_are_scanned_before_comments(literal):
    source = "const char* text() {\n#define TEXT " + literal + "\nreturn TEXT;\n}\n"
    entity, = audit_move.entities_of({"source.cpp": source})
    assert "#define TEXT " + literal in entity.normalized


def test_macro_digit_separators_and_comment_quotes():
    source = "int helper() {\n#define VALUE 1'000 // ignored ' and /*\nreturn VALUE;\n}\n"
    entity, = audit_move.entities_of({"source.cpp": source})
    assert "#define VALUE 1'000" in entity.normalized


def test_macro_line_splices_preserve_literal_contents():
    literal = '"a\\\nb"'
    source = "const char* text() {\n#define TEXT " + literal + "\nreturn TEXT;\n}\n"
    entity, = audit_move.entities_of({"source.cpp": source})
    assert "#define TEXT " + literal in entity.normalized


def test_macro_comments_and_layout_still_normalize():
    old = "int helper() {\n#define VALUE 1 /* comment */ + 2\nreturn VALUE;\n}\n"
    new = "int helper() {\n# define   VALUE  1 +  2\nreturn VALUE;\n}\n"
    assert audit_move.compare(
        audit_move.entities_of({"before.cpp": old}),
        audit_move.entities_of({"after.cpp": new}),
    ).definition_differences == 0


def test_function_like_macro_spacing_is_preserved():
    old = "int helper() {\n#define VALUE(x) (x)\nreturn VALUE(1);\n}\n"
    new = "int helper() {\n#define VALUE (x) (x)\nreturn VALUE(1);\n}\n"
    assert audit_move.compare(
        audit_move.entities_of({"before.cpp": old}),
        audit_move.entities_of({"after.cpp": new}),
    ).definition_differences == 2


@pytest.mark.parametrize("before,after,destination,differences", [
    ("static constexpr char token{0};", "inline constexpr char token{0};", "source.cpp", 2),
    ("int value(1);", "int value(2);", "source.cpp", 2),
    ('const char* text() {\n#define TEXT "a  b"\nreturn TEXT;\n}',
     'const char* text() {\n#define TEXT "a b"\nreturn TEXT;\n}', "source.cpp", 2),
    ("namespace original { static int helper(int value) { return value; } }",
     "namespace moved { inline int helper(int value) { return value; } }", "moved.cpp", 0),
])
def test_cli_audits_definitions_and_pure_moves(tmp_path, before, after, destination, differences):
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    source = tmp_path / "source.cpp"
    source.write_text(before, encoding="utf-8")
    subprocess.run(["git", "-C", str(tmp_path), "add", "source.cpp"], check=True)
    subprocess.run([
        "git", "-C", str(tmp_path), "-c", "user.name=iv8 tests",
        "-c", "user.email=iv8-tests@example.invalid", "-c", "commit.gpgsign=false",
        "-c", "core.hooksPath=" + str(tmp_path / "disabled-hooks"),
        "commit", "-q", "-m", "baseline",
    ], check=True)
    source.unlink()
    (tmp_path / destination).write_text(after, encoding="utf-8")
    result = subprocess.run(
        [sys.executable, str(tool_path), "--base", "HEAD"],
        cwd=tmp_path, capture_output=True, text=True,
    )
    assert result.returncode == (1 if differences else 0)
    assert f"RESULT: {differences} definition differences" in result.stdout
