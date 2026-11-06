"""Bill of materials from design.py.

Usage: python3 bom.py out/j2t-module-bom.csv [out/j2t-module-bom.md]
"""
import csv
import sys
from collections import OrderedDict

import design as D


def rows():
    groups: "OrderedDict[tuple, dict]" = OrderedDict()
    for p in D.P:
        key = (p.value, p.fp, p.mpn)
        g = groups.setdefault(key, {"refs": [], "value": p.value, "fp": p.fp.split(":")[1], "mpn": p.mpn,
                                    "note": p.note, "dnp": p.dnp})
        g["refs"].append(p.ref)
        if p.note and p.note not in g["note"]:
            g["note"] = (g["note"] + "; " + p.note).strip("; ")
    out = []
    for g in groups.values():
        refs = sorted(g["refs"], key=lambda r: (r.rstrip("0123456789"), int(r.lstrip("ABCDEFGHIJKLMNOPQRSTUVWXYZ") or 0)))
        out.append({"Designator": ", ".join(refs), "Qty": len(refs), "Value": g["value"], "Footprint": g["fp"],
                    "MPN": g["mpn"], "DNP": "yes" if g["dnp"] else "", "Note": g["note"]})
    out.sort(key=lambda r: (r["Designator"].rstrip("0123456789, ")[:1], r["Designator"]))
    return out


def main():
    rs = rows()
    with open(sys.argv[1], "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["Designator", "Qty", "Value", "Footprint", "MPN", "DNP", "Note"])
        w.writeheader()
        w.writerows(rs)
    if len(sys.argv) > 2:
        with open(sys.argv[2], "w") as f:
            f.write("| Designator | Qty | Value | Footprint | MPN / description |\n|---|---|---|---|---|\n")
            for r in rs:
                f.write(f"| {r['Designator']} | {r['Qty']} | {r['Value']} | {r['Footprint']} | {r['MPN']} |\n")
    print(f"{len(rs)} BOM lines, {sum(r['Qty'] for r in rs)} parts")


if __name__ == "__main__":
    main()
