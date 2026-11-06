"""Minimal KiCad s-expression reader/writer."""
import re

class Sym(str):
    """Bare symbol (unquoted token)."""
    __slots__ = ()

_tok = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))', re.S)

def parse(text):
    pos = 0
    stack = [[]]
    n = len(text)
    while pos < n:
        m = _tok.match(text, pos)
        if not m:
            break
        pos = m.end()
        if m.group(1):
            stack.append([])
        elif m.group(2):
            lst = stack.pop()
            stack[-1].append(lst)
        elif m.group(3) is not None:
            stack[-1].append(m.group(3).replace('\\"', '"'))
        elif m.group(4):
            stack[-1].append(Sym(m.group(4)))
    return stack[0]

def dump(node, indent=0):
    if isinstance(node, list):
        if not node:
            return "()"
        head = dump(node[0], indent)
        simple = all(not isinstance(c, list) for c in node)
        if simple:
            return "(" + " ".join(dump(c, indent) for c in node) + ")"
        # Keep the original child order: KiCad's parser is order sensitive in places
        # (e.g. "(pin_names (offset 0) hide)").
        s = "(" + head
        pad = "  " * (indent + 1)
        after_list = False
        for c in node[1:]:
            if isinstance(c, list):
                s += "\n" + pad + dump(c, indent + 1)
                after_list = True
            else:
                s += ("\n" + pad if after_list else " ") + dump(c, indent)
        s += "\n" + "  " * indent + ")"
        return s
    if isinstance(node, Sym):
        return str(node)
    if isinstance(node, str):
        return '"' + node.replace('"', '\\"') + '"'
    return str(node)

def find_all(node, head):
    return [c for c in node if isinstance(c, list) and c and c[0] == head]

def find(node, head):
    r = find_all(node, head)
    return r[0] if r else None

def load_symbol(lib, name):
    """Return the (symbol ...) node of `name` from a KiCad 7 symbol library."""
    doc = parse(open(f"/usr/share/kicad/symbols/{lib}.kicad_sym").read())[0]
    for s in find_all(doc, "symbol"):
        if s[1] == name:
            return s
    raise KeyError(f"{lib}:{name}")

def symbol_pins(sym):
    """All pins of a symbol including sub-units: list of dicts."""
    pins = []
    def walk(node, unit):
        for c in node:
            if isinstance(c, list) and c and c[0] == "symbol":
                walk(c, c[1])
            elif isinstance(c, list) and c and c[0] == "pin":
                at = find(c, "at")
                nm = find(c, "name")
                num = find(c, "number")
                ln = find(c, "length")
                pins.append(dict(number=num[1], name=nm[1], etype=str(c[1]), x=float(at[1]), y=float(at[2]),
                                 rot=float(at[3]) if len(at) > 3 else 0.0, length=float(ln[1]), unit=unit))
    walk(sym, sym[1])
    return pins

if __name__ == "__main__":
    import sys
    for spec in sys.argv[1:]:
        lib, name = spec.split(":")
        s = load_symbol(lib, name)
        ext = find(s, "extends")
        if ext:
            s = load_symbol(lib, ext[1])
        p = symbol_pins(s)
        print(f"== {spec} ({len(p)} pins)" + (f" extends {ext[1]}" if ext else ""))
        print("   " + "; ".join(f"{d['number']}:{d['name']}[{d['etype']}]" for d in p))
