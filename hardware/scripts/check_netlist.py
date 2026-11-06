import sys, design as D, sexp, sch_gen
doc = sexp.parse(open(sys.argv[1]).read())[0]
nets = {}
for n in sexp.find_all(sexp.find(doc, "nets"), "net"):
    name = sexp.find(n, "name")[1]
    nodes = [(sexp.find(x, "ref")[1], sexp.find(x, "pin")[1]) for x in sexp.find_all(n, "node")]
    if len(nodes) > 1 or not name.startswith("unconnected-"):
        nets[name.lstrip("/")] = sorted(nodes)
want = {k: sorted(v) for k, v in D.nets().items() if not k.startswith("NC_")}
sd = sch_gen.symdef("RF_Module", "ESP32-S3-WROOM-1")
fixed = {}
for k, v in want.items():
    out = []
    for ref, pin in v:
        if ref == "U3":
            out += [(ref, p["number"]) for p in sd.pins if p["name"] == pin or p["number"] == pin]
        else:
            out.append((ref, pin))
    fixed[k] = sorted(out)
bad = 0
for k in sorted(set(fixed) | set(nets)):
    a, b = fixed.get(k), nets.get(k)
    if a != b:
        bad += 1
        print("MISMATCH", k, "\n   design:", a, "\n   kicad :", b)
print("nets design", len(fixed), "kicad", len(nets), "mismatches", bad)
sys.exit(1 if bad else 0)
