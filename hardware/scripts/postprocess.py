"""Remove dangling vias/stubs reported by DRC, refill, re-run DRC, and print the remaining items."""
import re
import sys
import pcbnew

ORIGIN = 100.0


def items(rep, kind):
    out = []
    for bl in re.split(r"\n(?=\[)", open(rep).read()):
        if bl.startswith("[" + kind + "]"):
            out += [(float(m.group(1)), float(m.group(2))) for m in re.finditer(r"@\(([\d.]+) mm, ([\d.]+) mm\)", bl)]
    return out


def main(pcb):
    rep = pcb.replace(".kicad_pcb", "-drc.txt")
    board = pcbnew.LoadBoard(pcb)
    board.BuildConnectivity()
    # (the DRC report of the build is used as is: WriteDRCReport before editing breaks GetTracks)
    removed = 0
    tracks = list(board.GetTracks())   # one snapshot: iterating Tracks() after a Remove() fails
    doomed = []
    for (x, y) in items(rep, "via_dangling"):
        for t in tracks:
            if t.GetClass() == "PCB_VIA" and abs(t.GetPosition().x / 1e6 - x) < 1e-3 and abs(t.GetPosition().y / 1e6 - y) < 1e-3:
                doomed.append(t)
    for (x, y) in items(rep, "track_dangling"):
        for t in tracks:
            if t.GetClass() == "PCB_TRACK" and t.GetLength() / 1e6 < 1.0:
                for p in (t.GetStart(), t.GetEnd()):
                    if abs(p.x / 1e6 - x) < 1e-3 and abs(p.y / 1e6 - y) < 1e-3:
                        doomed.append(t)
                        break
    for t in {id(t): t for t in doomed}.values():
        board.Remove(t)
        removed += 1
    print("removed dangling items:", removed)
    for z in board.Zones():
        z.SetIsFilled(False)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(pcb, board)
    pcbnew.WriteDRCReport(board, rep, pcbnew.EDA_UNITS_MILLIMETRES, True)
    txt = open(rep).read()
    kinds = {}
    for m in re.finditer(r"\[(\w+)\]: (.*)", txt):
        kinds[m.group(1)] = kinds.get(m.group(1), 0) + 1
    print("DRC after post-processing:", kinds)
    print(re.search(r"\*\* Found (\d+) unconnected pads", txt).group(0))
    for bl in re.split(r"\n(?=\[)", txt):
        if bl.startswith("[") and not bl.startswith("[lib_footprint_issues]"):
            print("   " + " | ".join(l.strip() for l in bl.splitlines()[:3])[:220])


if __name__ == "__main__":
    main(sys.argv[1])
