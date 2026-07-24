"""Minimal KiCad s-expression parser/writer used by the generators."""

from __future__ import annotations


class Sym(str):
    """Bare (unquoted) s-expression atom."""


def parse(text: str):
    """Parse an s-expression string into nested lists of Sym/str/float."""
    i = 0
    n = len(text)

    def skip_ws():
        nonlocal i
        while i < n and text[i] in " \t\r\n":
            i += 1

    def atom():
        nonlocal i
        if text[i] == '"':
            i += 1
            out = []
            while text[i] != '"':
                if text[i] == "\\":
                    i += 1
                out.append(text[i])
                i += 1
            i += 1
            return "".join(out)
        j = i
        while i < n and text[i] not in ' \t\r\n()"':
            i += 1
        return Sym(text[j:i])

    def expr():
        nonlocal i
        skip_ws()
        if text[i] != "(":
            return atom()
        i += 1
        out = []
        while True:
            skip_ws()
            if text[i] == ")":
                i += 1
                return out
            out.append(expr())

    result = expr()
    return result


def dump(node, indent=0) -> str:
    """Serialize a parsed tree back to KiCad-style text."""
    if isinstance(node, Sym):
        return str(node)
    if isinstance(node, str):
        return '"' + node.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if not isinstance(node, list):
        return str(node)
    # list
    parts = [dump(c) for c in node]
    flat = "(" + " ".join(parts) + ")"
    if len(flat) <= 100 and "\n" not in flat:
        return flat
    pad = "  " * (indent + 1)
    out = "(" + (dump(node[0]) if node else "")
    rest = node[1:]
    # keep simple leading atoms on the same line
    k = 0
    while k < len(rest) and not isinstance(rest[k], list):
        out += " " + dump(rest[k])
        k += 1
    for c in rest[k:]:
        out += "\n" + pad + dump(c, indent + 1)
    out += "\n" + "  " * indent + ")"
    return out


def find_all(node, tag):
    """Yield child lists of `node` whose head atom equals `tag`."""
    for c in node:
        if isinstance(c, list) and c and isinstance(c[0], Sym) and str(c[0]) == tag:
            yield c


def find_one(node, tag):
    for c in find_all(node, tag):
        return c
    return None


def get_symbol_block(lib_text: str, name: str) -> str:
    """Extract the raw text of a top-level (symbol "name" ...) block."""
    needle = f'(symbol "{name}"'
    i = lib_text.index(needle)
    depth = 0
    j = i
    while True:
        if lib_text[j] == "(":
            depth += 1
        elif lib_text[j] == ")":
            depth -= 1
            if depth == 0:
                break
        j += 1
    return lib_text[i : j + 1]


def symbol_pins(sym_node):
    """Return [(number, name, etype, x, y, angle)] for a parsed lib symbol,

    collecting pins from all child unit symbols (…_0_1, …_1_1 etc.).
    Coordinates are symbol-space (Y up), position is the connection point.
    """
    pins = []

    def walk(node):
        for child in find_all(node, "symbol"):
            walk(child)
        for p in find_all(node, "pin"):
            etype = str(p[1])
            at = find_one(p, "at")
            x, y = float(at[1]), float(at[2])
            ang = float(at[3]) if len(at) > 3 else 0.0
            name = find_one(p, "name")[1]
            number = find_one(p, "number")[1]
            pins.append((str(number), str(name), etype, x, y, ang))

    walk(sym_node)
    return pins
