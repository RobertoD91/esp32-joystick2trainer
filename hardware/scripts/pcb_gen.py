"""Build the KiCad PCB from design.py with the pcbnew Python API, route it, fill zones, run DRC.

Usage: python3 pcb_gen.py out/j2t-module.kicad_pcb [--no-route] [--uuids out/j2t-module.kicad_sch.uuids.json]
"""
import json
import math
import os
import re
import sys

import pcbnew

import design as D
import router as R

FP_ROOT = "/usr/share/kicad/footprints/"
ORIGIN_X, ORIGIN_Y = 100.0, 100.0  # board origin on the KiCad sheet (mm)

mm = pcbnew.FromMM


def V(x, y):
    """Board-local mm -> VECTOR2I."""
    return pcbnew.VECTOR2I(mm(ORIGIN_X + x), mm(ORIGIN_Y + y))


def load_fp(fpid):
    lib, name = fpid.split(":")
    fp = pcbnew.FootprintLoad(FP_ROOT + lib + ".pretty", name)
    if fp is None:
        raise RuntimeError("footprint not found: " + fpid)
    return fp


def tighten_courtyard(fp, half_w, half_h):
    """Replace the courtyard (the WROOM's includes the antenna keep-out) by a tight rectangle,
    and drop the footprint's antenna keep-out rule area (the radio is not used)."""
    for z in list(fp.Zones()):
        fp.Remove(z)
    # drop the exposed-pad thermal vias: they are through holes that would block the other side
    for pad in list(fp.Pads()):
        if pad.GetName() == "41" and pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
            fp.Remove(pad)
    for item in list(fp.GraphicalItems()):
        if item.GetLayer() in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            fp.Remove(item)
    s = pcbnew.FP_SHAPE(fp)
    s.SetShape(pcbnew.SHAPE_T_RECT)
    s.SetStart(pcbnew.VECTOR2I(mm(-half_w), mm(-half_h)))
    s.SetEnd(pcbnew.VECTOR2I(mm(half_w), mm(half_h)))
    s.SetLayer(pcbnew.F_CrtYd)
    s.SetWidth(mm(0.05))
    fp.Add(s)


def courtyard_bbox(fp):
    """Courtyard bounding box in board-local mm: (x0, y0, x1, y1)."""
    layer = pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd
    poly = fp.GetCourtyard(layer)
    if poly.OutlineCount() == 0:
        bb = fp.GetBoundingBox(False, False)
    else:
        bb = poly.BBox()
    return (bb.GetLeft() / 1e6 - ORIGIN_X, bb.GetTop() / 1e6 - ORIGIN_Y,
            bb.GetRight() / 1e6 - ORIGIN_X, bb.GetBottom() / 1e6 - ORIGIN_Y)


def pack_regions(fps):
    """Shelf-pack the parts that have a region, in list order, rows from the top-left."""
    for rname, (side, x0, y0, x1, y1, gap) in D.REGIONS.items():
        parts = [p for p in D.P if p.region == rname]
        cx, cy, row_h = x0, y0, 0.0
        for part in parts:
            fp = fps[part.ref]
            bx0, by0, bx1, by1 = courtyard_bbox(fp)
            w, h = bx1 - bx0, by1 - by0
            if cx + w > x1 + 1e-6 and cx > x0:
                cx = x0
                cy += row_h + gap
                row_h = 0.0
            if cy + h > y1 + 1e-6:
                print(f"WARNING region {rname} overflow at {part.ref} (y {cy + h:.1f} > {y1})")
            # move so that the courtyard's top-left lands on the cursor
            dx, dy = cx - bx0, cy - by0
            pos = fp.GetPosition()
            fp.SetPosition(pcbnew.VECTOR2I(pos.x + mm(dx), pos.y + mm(dy)))
            part.x = (pos.x + mm(dx)) / 1e6 - ORIGIN_X
            part.y = (pos.y + mm(dy)) / 1e6 - ORIGIN_Y
            cx += w + gap
            row_h = max(row_h, h)


def add_routes(board, rt):
    """Replace every track and via on the board with the router's current state (idempotent, so
    it can be called after rebuilds)."""
    prune_dangling_vias(rt)
    for t in list(board.GetTracks()):
        board.Remove(t)
    for (net, layer, x1, y1, x2, y2, w) in rt.tracks:
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(V(x1, y1)); t.SetEnd(V(x2, y2)); t.SetWidth(mm(w))
        t.SetLayer(pcbnew.F_Cu if layer == 0 else pcbnew.B_Cu); t.SetNetCode(net)
        board.Add(t)
    for (net, x, y) in rt.vias:
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(V(x, y)); v.SetDrill(mm(R.VIA_DRILL)); v.SetWidth(mm(R.VIA_DIA))
        v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNetCode(net)
        board.Add(v)


def unconnected_pads(rep):
    """(ref, pad) pairs listed as unconnected in a DRC report."""
    txt = open(rep).read()
    out = set()
    for bl in re.split(r"\n(?=\[)", txt):
        if not bl.startswith("[unconnected_items]"):
            continue
        for m in re.finditer(r"[Pp]ad (\S+) \[([^\]]*)\] of (\S+)", bl):
            out.add((m.group(3), m.group(1), m.group(2)))
    return out


def build(out_path, do_route=True, uuids=None):
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_MinClearance = mm(0.15)
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_ViasMinSize = mm(0.5)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.3)
    ds.m_HoleToHoleMin = mm(0.25)
    ds.m_SolderMaskMinWidth = mm(0.0)
    for attr, val in (("m_MinSilkTextHeight", mm(0.5)), ("m_MinThermalReliefSpokeCount", 1),
                      ("m_MinSilkTextThickness", mm(0.08))):
        try:
            setattr(ds, attr, val)
        except Exception as e:  # noqa
            print("design setting not available:", attr, e)
    try:
        dnc = ds.m_NetSettings.m_DefaultNetClass
        dnc.SetClearance(mm(0.2))
        dnc.SetTrackWidth(mm(0.25))
        dnc.SetViaDiameter(mm(0.6))
        dnc.SetViaDrill(mm(0.3))
    except Exception as e:  # noqa
        print("netclass setup skipped:", e)

    # nets
    netcodes = {}
    for name in sorted(D.nets()):
        if name.startswith("NC_"):
            continue
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netcodes[name] = ni.GetNetCode()

    # footprints
    fps = {}
    for part in D.P:
        fp = load_fp(part.fp)
        fp.SetReference(part.ref)
        fp.SetValue(part.value)
        if part.ref == "U3":
            tighten_courtyard(fp, 9.3, 13.0)
        if uuids and part.ref in uuids.get("symbols", {}):
            fp.SetPath(pcbnew.KIID_PATH("/" + uuids["root"] + "/" + uuids["symbols"][part.ref]))
        if part.dnp:
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_DNP)
        board.Add(fp)
        # nets on pads (only resolvable once the footprint belongs to the board)
        pins = dict(part.pins)
        for pad in fp.Pads():
            name = pad.GetName()
            net = pins.get(name)
            if net is None and part.nc_unlisted:
                continue
            if net is None:
                if name == "":
                    continue  # mechanical pad
                raise RuntimeError(f"{part.ref}: pad {name} has no net")
            if net.startswith("NC_"):
                continue
            pad.SetNetCode(netcodes[net])
        # references on the fabrication layer: the silkscreen is too small for them
        fp.Reference().SetLayer(pcbnew.F_Fab)
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.5), mm(0.5)))
        fp.Reference().SetTextThickness(mm(0.08))
        fp.Value().SetVisible(False)
        fp.SetPosition(V(part.x, part.y))
        if part.side == "B":
            fp.Flip(fp.GetPosition(), True)
        fp.SetOrientationDegrees(part.rot)
        fps[part.ref] = fp
    pack_regions(fps)
    for rname in D.REGIONS:
        row = []
        for part in D.P:
            if part.region == rname:
                bx0, by0, bx1, by1 = courtyard_bbox(fps[part.ref])
                row.append(f"{part.ref}@{bx0:.1f},{by0:.1f} {bx1 - bx0:.1f}x{by1 - by0:.1f}")
        print("layout", rname, D.REGIONS[rname][1:5], "; ".join(row))
    for ref in ("J3", "J4", "U3"):
        print(ref, "courtyard", tuple(round(v, 2) for v in courtyard_bbox(fps[ref])))

    u3 = fps["U3"]
    def padpos(fp, name):
        for p in fp.Pads():
            if p.GetName() == name:
                return (round(p.GetPosition().x / 1e6 - ORIGIN_X, 2), round(p.GetPosition().y / 1e6 - ORIGIN_Y, 2))
    print(f"U3: pad14 {padpos(u3,'14')}, pad27 {padpos(u3,'27')}, pad15 {padpos(u3,'15')}, pad2 {padpos(u3,'2')}")

    # ESP32 pins by name -> make sure the nets landed (pad names are numbers in the footprint)
    sd_pins = None
    try:
        import sch_gen
        sd_pins = sch_gen.symdef("RF_Module", "ESP32-S3-WROOM-1").pins
    except Exception:
        pass
    if sd_pins:
        name2num = {p["name"]: p["number"] for p in sd_pins}
        part = D.by_ref("U3")
        for pad in u3.Pads():
            for pname, net in part.pins.items():
                nums = [name2num.get(pname, pname)] + [p["number"] for p in sd_pins if p["name"] == pname]
                if pad.GetName() in nums:
                    pad.SetNetCode(netcodes[net])

    print("stage: footprints done", flush=True)
    # outline with rounded corners
    W, H, r = D.BOARD_W, D.BOARD_H, D.CORNER_R
    def seg(x1, y1, x2, y2):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(V(x1, y1)); s.SetEnd(V(x2, y2))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)
    def arc(sx, sy, mx, my, ex, ey):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_ARC)
        s.SetArcGeometry(V(sx, sy), V(mx, my), V(ex, ey))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)
    k = r * (1 - math.sqrt(0.5))
    seg(r, 0, W - r, 0); seg(W, r, W, H - r); seg(W - r, H, r, H); seg(0, H - r, 0, r)
    arc(W - r, 0, W - k, k, W, r)
    arc(W, H - r, W - k, H - k, W - r, H)
    arc(r, H, k, H - k, 0, H - r)
    arc(0, r, k, k, r, 0)

    print("stage: outline done", flush=True)
    # silkscreen texts
    def txt(s, x, y, layer=pcbnew.F_SilkS, size=0.8, rot=0, mirror=False):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s); t.SetPosition(V(x, y)); t.SetLayer(layer)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(0.12))
        t.SetTextAngleDegrees(rot)
        if mirror:
            t.SetMirrored(True)
        board.Add(t)
    txt("j2t JR module rev A", 15.5, 48.3, size=0.6)
    txt("PPM HB VBAT GND SPORT", 11.0, 61.0, layer=pcbnew.B_SilkS, size=0.6, mirror=True)
    txt("JP1:p2 JP2:p4 GND|HB", 14.4, 49.6, size=0.5)
    txt("AUX GND SBUS CPPM", 4.6, 56.9, size=0.5)
    for ref in ("J1", "J2", "J5", "U4"):
        fp_ = fps[ref]
        print(ref, "courtyard", tuple(round(v, 2) for v in courtyard_bbox(fp_)), "pads",
              [(p.GetName(), round(p.GetPosition().x / 1e6 - ORIGIN_X, 2), round(p.GetPosition().y / 1e6 - ORIGIN_Y, 2),
                round(p.GetBoundingBox().GetWidth() / 1e6, 2), round(p.GetBoundingBox().GetHeight() / 1e6, 2))
               for p in fp_.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH])

    print("stage: texts done", flush=True)
    # ---------------------------------------------------------------- GND zones both sides
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNetCode(netcodes["GND"])
        z.SetZoneName("GND_" + ("F" if layer == pcbnew.F_Cu else "B"))
        ol = z.Outline()
        ol.NewOutline()
        for (x, y) in [(0, 0), (W, 0), (W, H), (0, H)]:
            ol.Append(mm(ORIGIN_X + x), mm(ORIGIN_Y + y))
        z.SetLocalClearance(mm(0.25))
        z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(mm(0.25))
        z.SetThermalReliefSpokeWidth(mm(0.35))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetIsFilled(False)
        board.Add(z)
    # solid pour connection for the ground pads: the board is reflowed, and the pours are too
    # fragmented for KiCad's two-spoke thermal rule to hold everywhere
    for fp_ in fps.values():
        for pad in fp_.Pads():
            if pad.GetNetCode() == netcodes["GND"]:
                pad.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
    rep = out_path.replace(".kicad_pcb", "-drc.txt")

    # ---------------------------------------------------------------- routing
    if do_route:
        pads = []
        for part in D.P:
            fp = fps[part.ref]
            for pad in fp.Pads():
                code = pad.GetNetCode()
                if code <= 0:
                    code = -2  # unconnected pad or mounting hole: an obstacle for every net
                bb = pad.GetBoundingBox()
                x = pad.GetPosition().x / 1e6 - ORIGIN_X
                y = pad.GetPosition().y / 1e6 - ORIGIN_Y
                layers = set()
                if pad.IsOnLayer(pcbnew.F_Cu):
                    layers.add(0)
                if pad.IsOnLayer(pcbnew.B_Cu):
                    layers.add(1)
                rp = R.Pad(part.ref, pad.GetName(), code, x, y, bb.GetWidth() / 1e6, bb.GetHeight() / 1e6, layers)
                rp.round = pad.GetShape() in (pcbnew.PAD_SHAPE_CIRCLE, pcbnew.PAD_SHAPE_OVAL)
                if pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                    rp.drill = max(pad.GetDrillSize().x, pad.GetDrillSize().y) / 1e6
                pads.append(rp)
        names = {v: k for k, v in netcodes.items()}
        widths = {netcodes[n]: 0.5 for n in D.POWER_NETS if n in netcodes}
        by_net_all = {}
        for p in pads:
            if p.net > 0:
                by_net_all.setdefault(p.net, []).append(p)
        gnd = netcodes["GND"]
        # The zone filler crashes on an in-memory BOARD in a headless script: save and reload once.
        pcbnew.SaveBoard(out_path, board)
        board = pcbnew.LoadBoard(out_path)
        board.BuildConnectivity()
        front = []
        rt = None
        for attempt in range(4):
            rt = R.Router(W, H, pads, widths, keepouts=[])
            rt.gnd = gnd
            by_net = {n: list(ps) for n, ps in by_net_all.items()}
            print(f"--- routing pass {attempt + 1}" + (f", first: {[names.get(n, n) for n in front]}" if front else ""), flush=True)
            # GND stitching vias in free spots: tie the two pours together early
            for (sx, sy) in [(3.0, 27.9), (9.0, 48.6)]:
                if rt.via_ok(*rt.cell(sx, sy), gnd):
                    rt.add_fixed_via(gnd, sx, sy)
                    vp = R.Pad("VIA", "GND", gnd, sx, sy, R.VIA_DIA, R.VIA_DIA, {0, 1})
                    ci, cj = rt.cell(sx, sy)
                    vp.core = {(0, ci, cj), (1, ci, cj)}
                    by_net[gnd].append(vp)
                else:
                    print("stitching via skipped at", sx, sy)

            def est(net):
                ps = by_net[net]
                xs = [p.x for p in ps]; ys = [p.y for p in ps]
                return (max(xs) - min(xs)) + (max(ys) - min(ys))
            power_first = [netcodes[n] for n in ["VBAT_IN", "VBAT_F", "VIN_PROT", "SW", "+5V", "VBUS_SW", "VBUS_A", "VBUS_C", "+3V3"] if n in netcodes]
            usb = [netcodes[n] for n in ["USB_DP", "USB_DM", "USBC_DP", "USBC_DM"]]
            # module fan-out first, then power, then the rest; ground is left to the pours
            mcu = sorted([n for n in by_net if n not in power_first and n != gnd and any(p.ref == "U3" for p in by_net[n])], key=est)
            rest = sorted([n for n in by_net if n not in power_first and n not in usb and n not in mcu and n != gnd], key=est)
            order = [n for n in usb if n in mcu] + mcu + power_first + [n for n in usb if n not in mcu] + rest
            order = list(dict.fromkeys(n for n in order if n != gnd))
            # Hand-prescribed fan-out of the USB-C pads: the D+/D- pairs are interleaved at 0.5 mm
            # pitch, so one pair loops under the connector and the other crosses with two vias;
            # CC and VBUS leave towards the connector body and drop to the bottom copper.
            j2 = {p.GetName(): (round(p.GetPosition().x / 1e6 - ORIGIN_X, 2), round(p.GetPosition().y / 1e6 - ORIGIN_Y, 2))
                  for p in fps["J2"].Pads()}
            xc = j2["A6"][0]
            yA6, yB6, yB7, yA7 = j2["A6"][1], j2["B6"][1], j2["B7"][1], j2["A7"][1]
            yA5, yB5, yA4, yA9 = j2["A5"][1], j2["B5"][1], j2["A4"][1], j2["A9"][1]
            xi, xo = round(xc - 1.2, 1), round(xc + 1.4, 1)          # 6.6 / 9.2 for xc = 7.8
            F_, B_ = 0, 1
            rt.add_fixed_path(netcodes["USBC_DP"], [(F_, xc, yA6), (F_, xi, yA6), (F_, xi, yB6), (F_, xc, yB6)], R.SIG_W)
            rt.add_fixed_path(netcodes["USBC_DP"], [(F_, xi, yB6), (F_, xi, yB6 + 0.3), (F_, xi - 0.9, yB6 + 0.3),
                                                     (B_, xi - 0.9, yB6 + 0.3)], R.SIG_W)
            rt.add_fixed_path(netcodes["USBC_DM"], [(F_, xc, yB7), (F_, xo, yB7), (B_, xo, yB7), (B_, xo, yA7),
                                                     (F_, xo, yA7), (F_, xc, yA7)], R.SIG_W)
            rt.add_fixed_path(netcodes["CC1"], [(F_, xc, yA5), (F_, xi - 0.1, yA5), (B_, xi - 0.1, yA5)], R.SIG_W)
            rt.add_fixed_path(netcodes["CC2"], [(F_, xc, yB5), (F_, xi - 0.1, yB5), (B_, xi - 0.1, yB5)], R.SIG_W)
            # VBUS pads leave towards the CH340 (the connector's NPTH holes sit on the other side) and
            # drop to the bottom copper where the diode and the ESD array are.
            xw = round(xc + 2.0, 1)                                     # 9.8
            rt.add_fixed_path(netcodes["VBUS_C"], [(F_, xc, yA4), (F_, xw + 0.2, yA4), (F_, xw + 0.2, yA9), (F_, xc, yA9)], 0.4)
            # module ground pin 40 sits at the top of the left pad column: tie it to the BOOT button's
            # ground pad on the other side (the pours take care of the rest)
            p40 = [p for p in by_net[gnd] if p.ref == "U3" and p.name == "40"][0]
            sw2 = [p for p in by_net[gnd] if p.ref == "SW2"][0]
            gx, gy = round(p40.x, 1), round(p40.y, 1)
            rt.add_fixed_path(gnd, [(B_, gx, gy), (B_, gx, 35.5), (F_, gx, 35.5), (F_, round(sw2.x, 1), 35.5),
                                    (F_, round(sw2.x, 1), round(sw2.y, 1))], R.SIG_W)
            rt.freeze_base()
            order = [n for n in front if n in order] + [n for n in order if n not in front]
            ok, fail, failures = rt.route(by_net, order, names, rip_budget=200)
            print(f"pass {attempt + 1}: routed {ok} connections, {fail} failed", flush=True)
            self_check(rt)
            add_routes(board, rt)
            refill(board, out_path, rep)
            comps = ground_phase(board, rt, gnd, by_net, names, set(order), out_path, rep)
            failed = sorted(n for n, f in rt.failures.items() if f)
            print(f"pass {attempt + 1} result: signal failures {[names.get(n, n) for n in failed]}, pour components {comps}", flush=True)
            for n in failed:
                for fp_ in rt.failures[n]:
                    cnt, blockers = rt.reach_report(fp_)
                    print(f"   reach {names.get(n, n):10s} {fp_}: {cnt} cells, walled by "
                          + ", ".join(f"{names.get(b, b)}x{c}" for b, c in blockers))
            if not failed and comps <= 1:
                break
            front = failed
        build.router = rt
        build.by_net = by_net
        build.names = names
        import pickle
        rt.log = None
        pickle.dump({"rt": rt, "netcodes": netcodes}, open(os.path.join(os.path.dirname(out_path), "rt.pkl"), "wb"))
        rt.log = print
    else:
        pcbnew.SaveBoard(out_path, board)
        board = pcbnew.LoadBoard(out_path)
        refill(board, out_path, rep)
    return board


def ground_phase(board, rt, gnd, by_net, names, rippable, out_path, rep):
    """Stitch pour islands with vias, route the remaining ground components (rip-up allowed),
    repeat while it helps. Returns the number of ground components left."""
    comps = 99
    for it in range(5):
        added = stitch_islands(board, rt, gnd)
        if added:
            add_routes(board, rt)
            refill(board, out_path, rep)
        routed = connect_gnd_components(board, rt, gnd, by_net, names, rippable)
        if routed:
            add_routes(board, rt)
            refill(board, out_path, rep)
        comps = len(gnd_graph(board, rt, gnd)[0])
        if comps <= 1 or (not added and not routed):
            break
    return comps


def refill(board, out_path, rep):
    for z in board.Zones():
        z.SetIsFilled(False)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(out_path, board)
    pcbnew.WriteDRCReport(board, rep, pcbnew.EDA_UNITS_MILLIMETRES, True)
    summarize_drc(rep)


def gnd_graph(board, rt, gnd):
    """Connectivity of the ground net as this script sees it: pour islands, pads, vias and
    tracks, joined where they overlap. Returns (components, island cells, polys)."""
    F, B = pcbnew.F_Cu, pcbnew.B_Cu
    polys = {z.GetLayer(): z.GetFilledPolysList(z.GetLayer()) for z in board.Zones() if z.GetNetCode() == gnd}
    lay = {F: 0, B: 1}
    islands = [(l, i) for l in (F, B) for i in range(polys[l].OutlineCount())]
    bboxes = {n: polys[n[0]].Outline(n[1]).BBox() for n in islands}

    def island_at(l, x, y):
        pt = V(x, y)
        for n in islands:
            if n[0] == l and bboxes[n].Contains(pt) and polys[l].Contains(pt, n[1]):
                return n
        return None
    pads = [p for p in rt.pads if p.net == gnd]
    vias = [(x, y) for (n, x, y) in rt.vias if n == gnd]
    tracks = [(l, x1, y1, x2, y2) for (n, l, x1, y1, x2, y2, w) in rt.tracks if n == gnd]
    nodes = [("I",) + n for n in islands] + [("P", p.ref, p.name) for p in pads] + \
            [("V", x, y) for (x, y) in vias] + [("T", k) for k in range(len(tracks))]
    parent = {n: n for n in nodes}

    def find(n):
        while parent[n] != n:
            parent[n] = parent[parent[n]]
            n = parent[n]
        return n

    def union(a, b):
        parent[find(a)] = find(b)

    def in_pad(p, l, x, y):
        return l in p.layers and abs(p.x - x) <= p.w / 2 + 1e-6 and abs(p.y - y) <= p.h / 2 + 1e-6
    for (x, y) in vias:
        for l in (F, B):
            isl = island_at(l, x, y)
            if isl:
                union(("V", x, y), ("I",) + isl)
        for p in pads:
            if any(in_pad(p, l, x, y) for l in p.layers):
                union(("V", x, y), ("P", p.ref, p.name))
    for p in pads:
        for l in p.layers:
            L = F if l == 0 else B
            probes = [(0.0, 0.0)]
            hw, hh = p.w / 2 + 0.08, p.h / 2 + 0.08
            n = max(2, int(max(p.w, p.h) / 0.4))
            for k in range(n + 1):
                t = -1 + 2 * k / n
                probes += [(t * hw, -hh), (t * hw, hh), (-hw, t * hh), (hw, t * hh)]
            for (dx, dy) in probes:
                isl = island_at(L, p.x + dx, p.y + dy)
                if isl:
                    union(("P", p.ref, p.name), ("I",) + isl)
                    break
    ends = {}
    for k, (l, x1, y1, x2, y2) in enumerate(tracks):
        L = F if l == 0 else B
        for (x, y) in ((x1, y1), (x2, y2), ((x1 + x2) / 2, (y1 + y2) / 2)):
            isl = island_at(L, x, y)
            if isl:
                union(("T", k), ("I",) + isl)
        for (x, y) in ((x1, y1), (x2, y2)):
            key = (l, round(x, 3), round(y, 3))
            if key in ends:
                union(("T", k), ends[key])
            ends[key] = ("T", k)
            for p in pads:
                if in_pad(p, l, x, y):
                    union(("T", k), ("P", p.ref, p.name))
            for (vx, vy) in vias:
                if abs(vx - x) < 1e-6 and abs(vy - y) < 1e-6:
                    union(("T", k), ("V", vx, vy))
    comps = {}
    for n in nodes:
        comps.setdefault(find(n), []).append(n)
    return comps, polys, islands, bboxes, pads


def rip_route(rt, net, src, tgt, tx, ty, by_net, names, rippable):
    """Connect two cell sets of `net`, ripping up and re-routing other nets if necessary."""
    path = rt.astar(net, 0, src, tgt, tx, ty, max_nodes=900000)
    if path:
        rt.commit(net, path, R.SIG_W)
        return True
    soft = {n for n in rippable if n != net and rt.rip_count[n] < R.MAX_RIPS_PER_NET}
    p = rt.astar(net, 0, src, tgt, tx, ty, soft=soft, max_nodes=900000)
    if p is None:
        return False
    vs = rt.victims(net, 0, p)
    for v in vs:
        rt.unroute(v)
    rt.rebuild()
    ok = rt.path_clean(net, 0, p)
    if ok:
        rt.commit(net, p, R.SIG_W)
    queue = sorted(vs)
    while queue:
        v = queue.pop(0)
        o, f, failed, ripped = rt.route_net(v, by_net[v], rt.net_widths.get(v, R.SIG_W), rippable)
        print(f"   re-routed {names.get(v, v)}: ok={o} fail={f}" + (f" ripped {[names.get(r, r) for r in ripped]}" if ripped else ""))
        for r in sorted(ripped):
            if r not in queue:
                queue.append(r)
    return ok


def connect_gnd_components(board, rt, gnd, by_net, names, rippable, max_routes=12):
    """Route tracks from every secondary ground component to the main one."""
    comps, polys, islands, bboxes, pads = gnd_graph(board, rt, gnd)
    if len(comps) <= 1:
        print("ground: one component")
        return 0
    F, B = pcbnew.F_Cu, pcbnew.B_Cu
    pad_by = {(p.ref, p.name): p for p in pads}
    via_cells = {}

    def island_cells(n):
        l, i = n
        bb = bboxes[n]
        x0, y0 = bb.GetLeft() / 1e6 - ORIGIN_X, bb.GetTop() / 1e6 - ORIGIN_Y
        x1, y1 = bb.GetRight() / 1e6 - ORIGIN_X, bb.GetBottom() / 1e6 - ORIGIN_Y
        cells = set()
        layer = 0 if l == F else 1
        y = round(y0, 1)
        while y <= y1:
            x = round(x0, 1)
            while x <= x1:
                if polys[l].Contains(V(x, y), i):
                    ci, cj = rt.cell(x, y)
                    if rt.free(0, layer, ci, cj, gnd):
                        cells.add((layer, ci, cj))
                x = round(x + 0.2, 1)
            y = round(y + 0.2, 1)
        return cells

    def comp_cells(members):
        cells = set()
        pts = []
        for m in members:
            if m[0] == "I":
                c = island_cells(m[1:])
                cells |= c
                if c:
                    pts.append(rt.pos(*list(c)[0][1:]))
            elif m[0] == "P":
                p = pad_by[m[1:]]
                cells |= p.core
                pts.append((p.x, p.y))
            elif m[0] == "V":
                ci, cj = rt.cell(m[1], m[2])
                cells |= {(0, ci, cj), (1, ci, cj)}
                pts.append((m[1], m[2]))
        return cells, pts
    main_root = max(comps, key=lambda r: sum(1 for m in comps[r] if m[0] in ("P", "V")) * 1000 + len(comps[r]))
    main_cells, main_pts = comp_cells(comps[main_root])
    print(f"   ground main component: {len(comps[main_root])} members, {len(main_cells)} cells, "
          f"e.g. {[m for m in comps[main_root] if m[0] != 'T'][:8]}")
    done = 0
    for root, members in comps.items():
        if root == main_root or done >= max_routes:
            continue
        if not any(m[0] in ("P", "V", "I") for m in members):
            continue
        src, pts = comp_cells(members)
        if not src or not main_cells:
            print(f"   ground component {members[:3]}: no cells (src {len(src)}, main {len(main_cells)})")
            continue
        cx = sum(p[0] for p in pts) / len(pts)
        cy = sum(p[1] for p in pts) / len(pts)
        tx, ty = min(main_pts, key=lambda q: math.hypot(q[0] - cx, q[1] - cy))
        if rip_route(rt, gnd, src, main_cells, tx, ty, by_net, names, rippable):
            main_cells |= src
            main_pts += pts
            done += 1
        else:
            desc = [m for m in members if m[0] in ("P", "V")][:3]
            print(f"   ground component near ({cx:.1f},{cy:.1f}) {desc}: no route to the main pour "
                  f"(src {len(src)} cells, main {len(main_cells)} cells)")
            for m in members:
                if m[0] == "P":
                    n, bl = rt.reach_report(pad_by[m[1:]])
                    print(f"      reach {m[1:]}: {n} cells, walled by " + ", ".join(f"{names.get(b, b)}x{c}" for b, c in bl))
    print(f"ground components: {len(comps)}, routed {done}")
    return done


def stitch_islands(board, rt, gnd, max_vias=12):
    """Join ground pour islands: wherever an island on one side overlaps an island of another
    connectivity component on the other side, drop a via. Returns the number of vias added."""
    F, B = pcbnew.F_Cu, pcbnew.B_Cu
    polys = {}
    for z in board.Zones():
        if z.GetNetCode() == gnd:
            polys[z.GetLayer()] = z.GetFilledPolysList(z.GetLayer())
    if F not in polys or B not in polys:
        return 0
    nodes = [(l, i) for l in (F, B) for i in range(polys[l].OutlineCount())]
    parent = {n: n for n in nodes}

    def find(n):
        while parent[n] != n:
            parent[n] = parent[parent[n]]
            n = parent[n]
        return n

    def union(a, b):
        parent[find(a)] = find(b)

    def island_at(l, x, y):
        pt = V(x, y)
        for i in range(polys[l].OutlineCount()):
            if polys[l].Contains(pt, i):
                return (l, i)
        return None
    through = [(x, y) for (n, x, y) in rt.vias if n == gnd] + [(x, y) for (n, x, y) in rt.fixed_vias if n == gnd]
    through += [(p.x, p.y) for p in rt.pads if p.net == gnd and len(p.layers) == 2]
    for (x, y) in through:
        a, b = island_at(F, x, y), island_at(B, x, y)
        if a and b:
            union(a, b)
    areas = {n: polys[n[0]].Outline(n[1]).Area() / 1e12 for n in nodes}
    pairs = []
    for a in nodes:
        if a[0] != F:
            continue
        bba = polys[F].Outline(a[1]).BBox()
        for b in nodes:
            if b[0] != B:
                continue
            bbb = polys[B].Outline(b[1]).BBox()
            if not bba.Intersects(bbb):
                continue
            pairs.append((-min(areas[a], areas[b]), a, b, bba, bbb))
    pairs.sort(key=lambda t: t[0])
    added = 0
    for _k, a, b, bba, bbb in pairs:
        if added >= max_vias or find(a) == find(b):
            continue
        x0 = max(bba.GetLeft(), bbb.GetLeft()) / 1e6 - ORIGIN_X
        y0 = max(bba.GetTop(), bbb.GetTop()) / 1e6 - ORIGIN_Y
        x1 = min(bba.GetRight(), bbb.GetRight()) / 1e6 - ORIGIN_X
        y1 = min(bba.GetBottom(), bbb.GetBottom()) / 1e6 - ORIGIN_Y
        # scan from the middle of the overlap outwards
        xs = sorted([round(x0 + k * 0.3, 1) for k in range(int((x1 - x0) / 0.3) + 1)], key=lambda v: abs(v - (x0 + x1) / 2))
        ys = sorted([round(y0 + k * 0.3, 1) for k in range(int((y1 - y0) / 0.3) + 1)], key=lambda v: abs(v - (y0 + y1) / 2))
        placed = False
        for y in ys:
            for x in xs:
                ci, cj = rt.cell(x, y)
                if not rt.via_ok(ci, cj, gnd):
                    continue
                pt = V(x, y)
                if polys[F].Contains(pt, a[1]) and polys[B].Contains(pt, b[1]):
                    rt.add_fixed_via(gnd, x, y)
                    union(a, b)
                    added += 1
                    placed = True
                    break
            if placed:
                break
    comps = len({find(n) for n in nodes})
    print(f"island stitching: {added} vias added, {comps} pour components remain")
    return added


def prune_dangling_vias(rt):
    """Drop routed vias that nothing of their net reaches on one of the layers (fixed ground
    stitching vias are kept: they join the pours)."""
    fixed = {(round(x, 3), round(y, 3)) for (_n, x, y) in rt.fixed_vias}
    for paths in rt.fixed_paths.values():
        for cells, _w in paths:
            for a, b in zip(cells, cells[1:]):
                if a[0] != b[0]:
                    x, y = rt.pos(a[1], a[2])
                    fixed.add((round(x, 3), round(y, 3)))
    segs = {}
    for (n, l, x1, y1, x2, y2, w) in rt.tracks:
        segs.setdefault((n, l), []).append((x1, y1, x2, y2, w))
    pads_by_net = {}
    for p in rt.pads:
        pads_by_net.setdefault(p.net, []).append(p)

    def on_segment(x, y, x1, y1, x2, y2, tol):
        dx, dy = x2 - x1, y2 - y1
        L = dx * dx + dy * dy
        t = 0.0 if L == 0 else max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / L))
        return math.hypot(x - (x1 + t * dx), y - (y1 + t * dy)) <= tol

    def covered(n, l, x, y):
        for (x1, y1, x2, y2, w) in segs.get((n, l), ()):
            if on_segment(x, y, x1, y1, x2, y2, w / 2 + 1e-3):
                return True
        for p in pads_by_net.get(n, []):
            if l in p.layers and abs(p.x - x) <= p.w / 2 + 1e-6 and abs(p.y - y) <= p.h / 2 + 1e-6:
                return True
        return False
    keep = []
    dropped = 0
    gnd = getattr(rt, "gnd", None)
    for (n, x, y) in rt.vias:
        key = (round(x, 3), round(y, 3))
        if n == gnd or key in fixed or all(covered(n, l, x, y) for l in (0, 1)):
            keep.append((n, x, y))
        else:
            dropped += 1
    for (n, x, y) in rt.vias:
        if (n, x, y) not in keep:
            print(f"   pruned via net{n} at ({x:.1f},{y:.1f})")
    rt.vias = keep
    print(f"pruned {dropped} dangling vias")


def self_check(rt, limit=0.2):
    """Geometric clearance check of the router's own output (tracks and vias of different nets)."""
    def seg_dist(a, b, c, d):
        def pd(p, a, b):
            ax, ay = a; bx, by = b; px, py = p
            dx, dy = bx - ax, by - ay
            L = dx * dx + dy * dy
            t = 0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
            return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

        def cross(o, a, b):
            return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
        if (cross(a, b, c) * cross(a, b, d) < 0) and (cross(c, d, a) * cross(c, d, b) < 0):
            return 0.0
        return min(pd(a, c, d), pd(b, c, d), pd(c, a, b), pd(d, a, b))
    worst = []
    tr = [(n, l, (x1, y1), (x2, y2), w) for (n, l, x1, y1, x2, y2, w) in rt.tracks]
    for i in range(len(tr)):
        n1, l1, a1, b1, w1 = tr[i]
        for j in range(i + 1, len(tr)):
            n2, l2, a2, b2, w2 = tr[j]
            if n1 == n2 or l1 != l2:
                continue
            if abs(a1[0] - a2[0]) > 3 and abs(b1[0] - b2[0]) > 3 and abs(a1[0] - b2[0]) > 3 and abs(b1[0] - a2[0]) > 3:
                continue
            g = seg_dist(a1, b1, a2, b2) - w1 / 2 - w2 / 2
            if g < limit - 1e-6:
                worst.append((round(g, 3), "trk", n1, n2, a1, a2))
    for (nv, vx, vy) in rt.vias:
        for (n, l, a, b, w) in tr:
            if n == nv:
                continue
            g = seg_dist((vx, vy), (vx, vy), a, b) - R.VIA_DIA / 2 - w / 2
            if g < limit - 1e-6:
                worst.append((round(g, 3), "via-trk", nv, n, (vx, vy), a))
        for (nv2, vx2, vy2) in rt.vias:
            if nv2 == nv or (vx2, vy2) <= (vx, vy):
                continue
            g = math.hypot(vx - vx2, vy - vy2) - R.VIA_DIA
            if g < limit - 1e-6:
                worst.append((round(g, 3), "via-via", nv, nv2, (vx, vy), (vx2, vy2)))
    worst.sort()
    print(f"self-check: {len(worst)} router clearance issues below {limit}")
    for w in worst[:8]:
        print("   ", w)


def summarize_drc(rep, details=("courtyards_overlap", "clearance", "hole_near_hole", "copper_edge_clearance",
                                "track_dangling", "via_dangling", "shorting_items", "items_not_allowed",
                                "hole_clearance", "starved_thermal")):
    txt = open(rep).read()
    kinds = {}
    for m in re.finditer(r"\[(\w+)\]: (.*)", txt):
        kinds[m.group(1)] = kinds.get(m.group(1), 0) + 1
    print("DRC:", dict(sorted(kinds.items(), key=lambda kv: -kv[1])))
    m = re.search(r"\*\* Found (\d+) unconnected pads", txt)
    if m:
        print("unconnected pads:", m.group(1))
    unc = []
    for bl in re.split(r"\n(?=\[)", txt):
        kind = bl[1:bl.find("]")] if bl.startswith("[") else ""
        if kind in details:
            lines = [l.strip() for l in bl.splitlines()[:4]]
            print("   " + " | ".join(lines)[:300])
        if kind == "unconnected_items":
            items = re.findall(r"@\(([\d.]+) mm, ([\d.]+) mm\): (.*)", bl)
            unc.append(" <-> ".join(f"{it[2][:40]}@({float(it[0]) - ORIGIN_X:.1f},{float(it[1]) - ORIGIN_Y:.1f})" for it in items))
    for u in unc[:30]:
        print("   unc:", u)


if __name__ == "__main__":
    out = sys.argv[1]
    do_route = "--no-route" not in sys.argv
    uuids = None
    if "--uuids" in sys.argv:
        uuids = json.load(open(sys.argv[sys.argv.index("--uuids") + 1]))
    build(out, do_route, uuids)
