"""Render design.py as a KiCad 7 schematic (.kicad_sch).

Style: every symbol gets a short wire stub and a net label on each pin (power nets get
power symbols). Symbols are grouped by function. Generated, not hand drawn; open it in
KiCad to tidy if you want, the netlist is what matters and it is checked by gen.sh.
"""
import copy
import math
import sys
import uuid
from typing import Dict, List, Tuple

import design as D
from sexp import Sym, dump, find, find_all, load_symbol, symbol_pins

GRID = 1.27
STUB = 2.54
ROOT_UUID = "a0b5d6f2-3c3e-4f1a-9d53-2a1f0e9c1001"


def uid():
    return str(uuid.uuid4())


def snap(v):
    return round(round(v / GRID) * GRID, 2)


# ----------------------------------------------------------------------------- symbols
class SymDef:
    def __init__(self, lib, name):
        self.lib, self.name = lib, name
        node = load_symbol(lib, name)
        ext = find(node, "extends")
        if ext:
            parent = load_symbol(lib, ext[1])
            flat = copy.deepcopy(parent)
            pname = parent[1]
            # rename sub-units Parent_u_s -> Child_u_s
            for c in flat:
                if isinstance(c, list) and c and c[0] == "symbol" and c[1].startswith(pname + "_"):
                    c[1] = name + c[1][len(pname):]
            # child property overrides
            child_props = {p[1]: p for p in find_all(node, "property")}
            for i, c in enumerate(flat):
                if isinstance(c, list) and c and c[0] == "property" and c[1] in child_props:
                    flat[i] = copy.deepcopy(child_props[c[1]])
            for p in child_props.values():
                if not any(isinstance(c, list) and c and c[0] == "property" and c[1] == p[1] for c in flat):
                    flat.insert(2, copy.deepcopy(p))
            node = flat
        node = copy.deepcopy(node)
        node[1] = f"{lib}:{name}"
        self.node = node
        self.pins = symbol_pins(node)
        xs, ys = [], []
        for p in self.pins:
            xs.append(p["x"]); ys.append(p["y"])
            dx, dy = math.cos(math.radians(p["rot"])), math.sin(math.radians(p["rot"]))
            xs.append(p["x"] + dx * p["length"]); ys.append(p["y"] + dy * p["length"])
        def walk(n):
            for c in n:
                if isinstance(c, list) and c:
                    if c[0] in ("rectangle",):
                        s, e = find(c, "start"), find(c, "end")
                        xs.extend([float(s[1]), float(e[1])]); ys.extend([float(s[2]), float(e[2])])
                    elif c[0] in ("polyline",):
                        for xy in find_all(find(c, "pts"), "xy"):
                            xs.append(float(xy[1])); ys.append(float(xy[2]))
                    elif c[0] == "circle":
                        cc, r = find(c, "center"), float(find(c, "radius")[1])
                        xs.extend([float(cc[1]) - r, float(cc[1]) + r]); ys.extend([float(cc[2]) - r, float(cc[2]) + r])
                    elif c[0] == "symbol":
                        walk(c)
        walk(node)
        if not xs:
            xs, ys = [0], [0]
        self.bbox = (min(xs), min(ys), max(xs), max(ys))  # lib coords (y up)

    def pin_by(self, key):
        for p in self.pins:
            if p["number"] == key or p["name"] == key:
                return p
        raise KeyError(f"{self.lib}:{self.name} pin {key}")


_symcache: Dict[Tuple[str, str], SymDef] = {}


def symdef(lib, name):
    if (lib, name) not in _symcache:
        _symcache[(lib, name)] = SymDef(lib, name)
    return _symcache[(lib, name)]


# ----------------------------------------------------------------------------- drawing primitives
items: List[list] = []
pwr_count = 0


def wire(x1, y1, x2, y2):
    items.append([Sym("wire"), [Sym("pts"), [Sym("xy"), x1, y1], [Sym("xy"), x2, y2]],
                  [Sym("stroke"), [Sym("width"), 0], [Sym("type"), Sym("default")]], [Sym("uuid"), uid()]])


def label(net, x, y, rot):
    just = {0: "left", 180: "right", 90: "left", 270: "right"}[rot]
    items.append([Sym("label"), net, [Sym("at"), x, y, rot], [Sym("fields_autoplaced")],
                  [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], [Sym("justify"), Sym(just), Sym("bottom")]],
                  [Sym("uuid"), uid()]])


def no_connect(x, y):
    items.append([Sym("no_connect"), [Sym("at"), x, y], [Sym("uuid"), uid()]])


def text(s, x, y, size=2.0, bold=False):
    font = [Sym("font"), [Sym("size"), size, size]]
    if bold:
        font.append(Sym("bold"))
    items.append([Sym("text"), s, [Sym("at"), x, y, 0], [Sym("effects"), font, [Sym("justify"), Sym("left"), Sym("bottom")]],
                  [Sym("uuid"), uid()]])


def prop(name, value, x, y, hide=False, justify="left"):
    eff = [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], [Sym("justify"), Sym(justify)]]
    if hide:
        eff.append(Sym("hide"))
    return [Sym("property"), name, value, [Sym("at"), x, y, 0], eff]


def power_symbol(libname, x, y, value=None):
    """Place a power symbol (power:GND, power:+5V, power:PWR_FLAG) with its pin at (x, y)."""
    global pwr_count
    pwr_count += 1
    lib, name = libname.split(":")
    sd = symdef(lib, name)
    ref = f"#PWR{pwr_count:03d}"
    u = uid()
    val = value or name
    sym = [Sym("symbol"), [Sym("lib_id"), libname], [Sym("at"), x, y, 0], [Sym("unit"), 1],
           [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("no")],
           [Sym("fields_autoplaced")], [Sym("uuid"), u],
           prop("Reference", ref, x, y + 5, hide=True),
           prop("Value", val, x, y + (3.81 if name == "GND" else -3.81) if name != "PWR_FLAG" else y - 3.81),
           prop("Footprint", "", x, y, hide=True), prop("Datasheet", "", x, y, hide=True)]
    for p in sd.pins:
        sym.append([Sym("pin"), p["number"], [Sym("uuid"), uid()]])
    sym.append([Sym("instances"), [Sym("project"), D.PROJECT, [Sym("path"), "/" + ROOT_UUID,
                                                                 [Sym("reference"), ref], [Sym("unit"), 1]]]])
    items.append(sym)
    return u


def attach_net(net, ex, ey, dx, dy):
    """Terminate a stub ending at (ex, ey) heading (dx, dy) on `net`."""
    if net.startswith("NC_"):
        raise RuntimeError("NC nets are handled before stubs")
    if net in D.POWER_SYMBOL_NETS:
        # Power symbol when the stub already points the right way (GND down, rails up);
        # otherwise a plain label with the power net name, which is electrically the same.
        want = (0, 1) if net == "GND" else (0, -1)
        if (dx, dy) == want:
            power_symbol(D.POWER_SYMBOL_NETS[net], ex, ey)
            return
    rot = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}[(dx, dy)]
    label(net, ex, ey, rot)


def place_part(part: D.Part, X, Y) -> str:
    """Place a design part with its origin at (X, Y). Returns the symbol uuid."""
    sd = symdef(part.lib, part.sym)
    u = uid()
    rot = part.sch_rot
    x0, y0, x1, y1 = sd.bbox
    # property positions: reference top-left, value bottom-left
    if rot == 0:
        top = Y - y1
        bot = Y - y0
        left = X + x0
    else:
        top = Y + y0
        bot = Y + y1
        left = X - x1
    sym = [Sym("symbol"), [Sym("lib_id"), f"{part.lib}:{part.sym}"], [Sym("at"), X, Y, rot], [Sym("unit"), 1],
           [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("yes" if part.dnp else "no")],
           [Sym("uuid"), u],
           prop("Reference", part.ref, snap(left), snap(top - 1.27)),
           prop("Value", part.value, snap(left), snap(bot + 2.54)),
           prop("Footprint", part.fp, X, Y, hide=True),
           prop("Datasheet", "~", X, Y, hide=True)]
    if part.mpn:
        sym.append(prop("MPN", part.mpn, X, Y, hide=True))
    if part.lcsc:
        sym.append(prop("LCSC", part.lcsc, X, Y, hide=True))
    for p in sd.pins:
        sym.append([Sym("pin"), p["number"], [Sym("uuid"), uid()]])
    sym.append([Sym("instances"), [Sym("project"), D.PROJECT, [Sym("path"), "/" + ROOT_UUID,
                                                                 [Sym("reference"), part.ref], [Sym("unit"), 1]]]])
    items.append(sym)

    # pins -> stubs
    seen_nets_per_pin = {}
    for p in sd.pins:
        net = part.pins.get(p["number"]) or part.pins.get(p["name"])
        if net is None:
            if part.nc_unlisted:
                net = "NC_" + p["number"]
            else:
                raise RuntimeError(f"{part.ref}: pin {p['number']} ({p['name']}) has no net")
        if rot == 0:
            cx, cy = X + p["x"], Y - p["y"]
            ang = p["rot"]
        else:
            cx, cy = X - p["x"], Y + p["y"]
            ang = (p["rot"] + 180) % 360
        cx, cy = round(cx, 2), round(cy, 2)
        if net.startswith("NC_"):
            no_connect(cx, cy)
            continue
        dx, dy = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[int(ang) % 360]
        ex, ey = round(cx + dx * STUB, 2), round(cy + dy * STUB, 2)
        wire(cx, cy, ex, ey)
        attach_net(net, ex, ey, dx, dy)
    return u


def part_cell(part):
    sd = symdef(part.lib, part.sym)
    x0, y0, x1, y1 = sd.bbox
    longest = max((len(n) for n in part.pins.values()), default=4)
    side = STUB + 1.5 + 1.0 * longest
    w = (x1 - x0) + 2 * side
    h = (y1 - y0) + 2 * (STUB + 6.0)
    return w, h, (x0, y0, x1, y1)


def main(out_path):
    global items
    items = []
    uuids: Dict[str, str] = {}
    sheet_w = 841.0
    margin = 15.0
    groups = ["power_in", "bay", "power_mux", "ldo", "usb_a", "mcu", "usb_c", "outputs", "misc"]
    cur_x, cur_y, band_h = margin, 30.0, 0.0
    for g in groups:
        parts = [p for p in D.P if p.group == g]
        cells = [part_cell(p) for p in parts]
        # flow layout inside the group, max width 260 mm
        maxw = 300.0
        rows: List[List[int]] = [[]]
        rw = 0.0
        for i, (w, h, _) in enumerate(cells):
            if rw + w > maxw and rows[-1]:
                rows.append([]); rw = 0.0
            rows[-1].append(i); rw += w
        gw = max(sum(cells[i][0] for i in r) for r in rows)
        gh = sum(max(cells[i][1] for i in r) for r in rows) + 10
        if cur_x + gw > sheet_w - margin:
            cur_x = margin
            cur_y += band_h + 15
            band_h = 0.0
        text(D.GROUP_TITLES[g], snap(cur_x), snap(cur_y - 2), size=2.5, bold=True)
        gy = cur_y + 8
        for r in rows:
            rh = max(cells[i][1] for i in r)
            gx = cur_x
            for i in r:
                w, h, (x0, y0, x1, y1) = cells[i]
                part = parts[i]
                # origin so that bbox centre sits in the cell centre
                X = snap(gx + w / 2 - (x0 + x1) / 2)
                Y = snap(gy + rh / 2 + (y0 + y1) / 2)
                uuids[part.ref] = place_part(part, X, Y)
                gx += w
            gy += rh
        band_h = max(band_h, gh)
        cur_x += gw + 15
    # power flags
    cur_x, cur_y = margin, cur_y + band_h + 20
    text("Power flags (ERC)", snap(cur_x), snap(cur_y - 2), size=2.5, bold=True)
    fx = cur_x + 10
    for net in D.PWR_FLAG_NETS:
        x, y = snap(fx), snap(cur_y + 10)
        power_symbol("power:PWR_FLAG", x, y)
        wire(x, y, x, y + STUB)
        label(net, x, y + STUB, 270)
        fx += 22
    notes = [
        "esp32-joystick2trainer JR bay module, rev A. Generated from hardware/scripts/design.py.",
        "JR bay pins: 1 PPM, 2 heartbeat, 3 VBAT, 4 GND, 5 S.PORT (verify on the radio; JP1/JP2 select GND/heartbeat on pins 2/4).",
        "Bay supply 2S (6.0-8.4 V) only: Q1 Vgs rating. USB-C 5 V powers the board when the bay supply is absent (no back-feed).",
        "Firmware pins: SBUS GPIO17, CPPM GPIO18, USB host GPIO19/20, LED GPIO38, VBUS enable GPIO21, I2C GPIO8/9, VBAT sense GPIO1.",
    ]
    for i, n in enumerate(notes):
        text(n, snap(cur_x), snap(cur_y + 30 + i * 4), size=2.0)

    lib_symbols = [Sym("lib_symbols")]
    for sd in _symcache.values():
        lib_symbols.append(sd.node)
    doc = [Sym("kicad_sch"), [Sym("version"), 20230121], [Sym("generator"), Sym("eeschema")],
           [Sym("uuid"), ROOT_UUID], [Sym("paper"), "A1"],
           [Sym("title_block"), [Sym("title"), D.TITLE], [Sym("date"), "2026-11-06"], [Sym("rev"), D.REV],
            [Sym("company"), "esp32-joystick2trainer (MIT firmware, CERN-OHL-P-2.0 hardware)"]],
           lib_symbols] + items + [[Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]]]
    with open(out_path, "w") as f:
        f.write(dump(doc) + "\n")
    return uuids


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "out/j2t-module.kicad_sch"
    u = main(out)
    import json
    with open(out + ".uuids.json", "w") as f:
        json.dump({"root": ROOT_UUID, "symbols": u}, f, indent=1)
    print(f"wrote {out}: {len(D.P)} parts, {len(_symcache)} symbol definitions")
