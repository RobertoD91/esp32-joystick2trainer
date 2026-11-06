"""A small grid router for two-layer boards (A* on a 0.1 mm grid, 8 neighbours, vias,
rip-up and re-route).

Good enough for a dense prototype: Manhattan/45-degree traces, conservative clearances,
every result is checked afterwards by KiCad's own DRC. Coordinates are board-local mm.

Occupancy is kept per "class": a cell is usable by a signal trace, a power trace or a via
when its centre is at least (clearance + half width) away from any copper of another net.
When a connection cannot be made, a second search may cross cells owned by other nets at a
cost; those nets are ripped up, the grids rebuilt from the base state plus the remaining
routes, and the victims are queued to be routed again.
"""
import heapq
import math
from collections import Counter
from typing import Dict, List, Optional, Set, Tuple

PITCH = 0.1
CLEARANCE = 0.24          # routed clearance (DRC asks 0.2; margin for the grid)
EDGE_CLEARANCE = 0.3
VIA_DIA = 0.6
VIA_DRILL = 0.3
LAYERS = (0, 1)           # 0 = F.Cu, 1 = B.Cu
SIG_W = 0.2
PWR_W = 0.5
CLASSES = {0: SIG_W / 2, 1: PWR_W / 2, 2: VIA_DIA / 2}   # half widths per class
MULTI = -1
VIA_COST = 20.0
WRONG_DIR = 1.6           # cost factor for horizontal runs on F.Cu / vertical runs on B.Cu
SOFT_COST = 30.0
MAX_RIPS_PER_NET = 6

Cell = Tuple[int, int, int]


class Pad:
    def __init__(self, ref, name, net, x, y, w, h, layers):
        self.ref, self.name, self.net = ref, name, net
        self.x, self.y, self.w, self.h = x, y, w, h
        self.layers = layers
        self.round = False
        self.drill = 0.0          # hole diameter for through-hole pads (hole-to-hole rule)
        self.core: Set[Cell] = set()

    def __repr__(self):
        return f"Pad({self.ref}.{self.name} net{self.net} @{self.x:.2f},{self.y:.2f} {self.w:.2f}x{self.h:.2f} L{sorted(self.layers)})"


class Router:
    def __init__(self, width, height, pads: List[Pad], net_widths: Dict[int, float],
                 keepouts: Optional[List[Tuple[int, float, float, float, float]]] = None, log=print):
        self.W, self.H = width, height
        self.nx = int(round(width / PITCH)) + 1
        self.ny = int(round(height / PITCH)) + 1
        self.pads = pads
        self.net_widths = net_widths
        self.keepouts = keepouts or []
        self.log = log
        self.grid = {c: [[0] * (self.nx * self.ny) for _ in LAYERS] for c in CLASSES}
        self.core_owner: Dict[Cell, int] = {}
        self.tracks: List[Tuple[int, int, float, float, float, float, float]] = []
        self.vias: List[Tuple[int, float, float]] = []
        self.committed: Dict[int, List[Tuple[List[Cell], float]]] = {}
        self.fixed_vias: List[Tuple[int, float, float]] = []
        self.fixed_paths: Dict[int, List[Tuple[List[Cell], float]]] = {}
        self.rip_count: Counter = Counter()
        self.rips = 0
        self.failures: Dict[int, List[Pad]] = {}
        self._build()
        self.base = {c: [g.copy() for g in self.grid[c]] for c in CLASSES}

    # ------------------------------------------------------------------ geometry helpers
    def idx(self, i, j):
        return j * self.nx + i

    def cell(self, x, y):
        return int(round(x / PITCH)), int(round(y / PITCH))

    def pos(self, i, j):
        return i * PITCH, j * PITCH

    def _mark(self, g, i0, j0, i1, j1, net, test):
        for j in range(max(0, j0), min(self.ny - 1, j1) + 1):
            base = j * self.nx
            for i in range(max(0, i0), min(self.nx - 1, i1) + 1):
                if not test(i, j):
                    continue
                k = base + i
                v = g[k]
                if v == 0:
                    g[k] = net
                elif v != net:
                    g[k] = MULTI

    def mark_rect_all(self, layer, x0, y0, x1, y1, net):
        """Copper rectangle of `net`: expand per class and mark."""
        for c, hw in CLASSES.items():
            e = CLEARANCE + hw
            i0, j0 = int(math.floor((x0 - e) / PITCH)), int(math.floor((y0 - e) / PITCH))
            i1, j1 = int(math.ceil((x1 + e) / PITCH)), int(math.ceil((y1 + e) / PITCH))
            ex0, ey0, ex1, ey1 = x0 - e, y0 - e, x1 + e, y1 + e
            self._mark(self.grid[c][layer], i0, j0, i1, j1, net,
                       lambda i, j: ex0 - 1e-9 <= i * PITCH <= ex1 + 1e-9 and ey0 - 1e-9 <= j * PITCH <= ey1 + 1e-9)

    def mark_disc_all(self, layer, cx, cy, r, net):
        """Copper disc of radius r of `net`."""
        for c, hw in CLASSES.items():
            rr = r + CLEARANCE + hw
            i0, j0 = self.cell(cx - rr, cy - rr)
            i1, j1 = self.cell(cx + rr, cy + rr)
            r2 = rr * rr
            self._mark(self.grid[c][layer], i0, j0, i1, j1, net,
                       lambda i, j: (i * PITCH - cx) ** 2 + (j * PITCH - cy) ** 2 <= r2)

    def block_rect(self, layer, x0, y0, x1, y1):
        for c in (0, 1):  # traces only: vias may still pass through
            i0, j0 = int(math.floor(x0 / PITCH)), int(math.floor(y0 / PITCH))
            i1, j1 = int(math.ceil(x1 / PITCH)), int(math.ceil(y1 / PITCH))
            self._mark(self.grid[c][layer], i0, j0, i1, j1, MULTI, lambda i, j: True)

    def _build(self):
        for layer in LAYERS:
            for c, hw in CLASSES.items():
                m = EDGE_CLEARANCE + hw
                g = self.grid[c][layer]
                self._mark(g, 0, 0, self.nx - 1, self.ny - 1, MULTI,
                           lambda i, j, m=m: i * PITCH < m or i * PITCH > self.W - m or j * PITCH < m or j * PITCH > self.H - m)
            for kl, x0, y0, x1, y1 in self.keepouts:
                if kl == layer:
                    self.block_rect(layer, x0, y0, x1, y1)
        for p in self.pads:
            for layer in p.layers:
                self.mark_rect_all(layer, p.x - p.w / 2, p.y - p.h / 2, p.x + p.w / 2, p.y + p.h / 2, p.net)
        # no via drill next to another hole: hole-to-hole clearance 0.25 mm plus margin
        for p in self.pads:
            if p.drill > 0:
                rr = p.drill / 2 + VIA_DRILL / 2 + 0.3
                i0, j0 = self.cell(p.x - rr, p.y - rr)
                i1, j1 = self.cell(p.x + rr, p.y + rr)
                for layer in LAYERS:
                    self._mark(self.grid[2][layer], i0, j0, i1, j1, MULTI,
                               lambda i, j, p=p, r2=rr * rr: (i * PITCH - p.x) ** 2 + (j * PITCH - p.y) ** 2 <= r2)
        # pad cores belong to their net whatever the neighbours say (entry/exit cells)
        for p in self.pads:
            cores = set()
            for layer in p.layers:
                wi = max(0.0, p.w / 2 - SIG_W / 2)
                hi = max(0.0, p.h / 2 - SIG_W / 2)
                ci, cj = self.cell(p.x, p.y)
                cells = {(layer, ci, cj)}
                i0, j0 = self.cell(p.x - wi, p.y - hi)
                i1, j1 = self.cell(p.x + wi, p.y + hi)
                rr = min(wi, hi)
                for j in range(j0, j1 + 1):
                    for i in range(i0, i1 + 1):
                        dx, dy = i * PITCH - p.x, j * PITCH - p.y
                        if p.round:
                            if dx * dx + dy * dy <= rr * rr + 1e-9:
                                cells.add((layer, i, j))
                        elif abs(dx) <= wi + 1e-9 and abs(dy) <= hi + 1e-9:
                            cells.add((layer, i, j))
                cells = {c for c in cells if 0 <= c[1] < self.nx and 0 <= c[2] < self.ny}
                for (l, i, j) in cells:
                    for c in (0, 1):
                        self.grid[c][l][self.idx(i, j)] = p.net
                    self.core_owner[(l, i, j)] = p.net
                cores |= cells
            p.core = cores

    def freeze_base(self):
        """Make the current grid (pads plus hand-prescribed routes and vias) the base state, so
        that rip-up never tries to cross copper it cannot remove."""
        self.base = {c: [g.copy() for g in self.grid[c]] for c in CLASSES}

    def rebuild(self):
        """Base state plus the fixed vias and every committed route."""
        self.grid = {c: [g.copy() for g in self.base[c]] for c in CLASSES}
        self.tracks, self.vias = [], []
        for (net, x, y) in self.fixed_vias:
            self.add_via(net, x, y)
        for net, paths in self.fixed_paths.items():
            for path, width in paths:
                self._mark_path(net, path, width)
        for net, paths in self.committed.items():
            for path, width in paths:
                self._mark_path(net, path, width)

    # ------------------------------------------------------------------ passability
    def free(self, cls, layer, i, j, net):
        if i < 0 or j < 0 or i >= self.nx or j >= self.ny:
            return False
        v = self.grid[cls][layer][self.idx(i, j)]
        return v == 0 or v == net

    def via_ok(self, i, j, net):
        return self.free(2, 0, i, j, net) and self.free(2, 1, i, j, net)

    def path_clean(self, net, cls, path):
        for a, b in zip(path, path[1:]):
            if not self.free(cls, b[0], b[1], b[2], net):
                return False
            if a[0] != b[0] and not self.via_ok(a[1], a[2], net):
                return False
        return self.free(cls, path[0][0], path[0][1], path[0][2], net)

    # ------------------------------------------------------------------ A*
    def astar(self, net, cls, sources, targets, tx, ty, soft: Optional[Set[int]] = None, max_nodes=600000):
        """Shortest path from any source cell to any target cell. With `soft`, cells owned by
        those nets may be crossed at a cost (their pad cores never)."""
        nx, ny = self.nx, self.ny
        g0 = self.grid[cls]
        g2 = self.grid[2]
        b0 = self.base[cls]
        b2 = self.base[2]

        # a cell owned by another net is only "soft" when that ownership comes from a route
        # (the base state holds pads and edges, which a rip-up cannot remove)
        def step(l, i, j):
            if i < 0 or j < 0 or i >= nx or j >= ny:
                return None
            k = j * nx + i
            v = g0[l][k]
            if v == 0 or v == net:
                return 0.0
            if soft and b0[l][k] == 0:
                if v > 0 and v in soft:
                    return SOFT_COST
                if v == MULTI:
                    return 2 * SOFT_COST   # several routes too close together: all of them go
            return None

        def via_step(i, j):
            tot = 0.0
            k = j * nx + i
            for l in LAYERS:
                v = g2[l][k]
                if v == 0 or v == net:
                    continue
                if soft and b2[l][k] == 0 and ((v > 0 and v in soft) or v == MULTI):
                    tot += SOFT_COST if v > 0 else 2 * SOFT_COST
                    continue
                return None
            return tot

        def h(l, i, j):
            return math.hypot(i * PITCH - tx, j * PITCH - ty) / PITCH  # admissible: preferred-direction cost is 1

        openh = []
        best = {}
        for s in sources:
            best[s] = 0.0
            heapq.heappush(openh, (h(*s), 0.0, s))
        came = {}
        nodes = 0
        # layer direction preference: vertical on F.Cu, horizontal on B.Cu (keeps the pours in
        # larger pieces and leaves room for vias at the crossings)
        dirs_by_layer = {
            0: [(1, 0, WRONG_DIR), (-1, 0, WRONG_DIR), (0, 1, 1.0), (0, -1, 1.0),
                (1, 1, 1.4142 * 1.25), (-1, 1, 1.4142 * 1.25), (1, -1, 1.4142 * 1.25), (-1, -1, 1.4142 * 1.25)],
            1: [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, WRONG_DIR), (0, -1, WRONG_DIR),
                (1, 1, 1.4142 * 1.25), (-1, 1, 1.4142 * 1.25), (1, -1, 1.4142 * 1.25), (-1, -1, 1.4142 * 1.25)],
        }
        while openh:
            f, g, cur = heapq.heappop(openh)
            if best.get(cur, 1e18) < g - 1e-9:
                continue
            if cur in targets:
                path = [cur]
                while cur in came:
                    cur = came[cur]
                    path.append(cur)
                return path[::-1]
            nodes += 1
            if nodes > max_nodes:
                return None
            l, i, j = cur
            for di, dj, c in dirs_by_layer[l]:
                ni, nj = i + di, j + dj
                sc = step(l, ni, nj)
                if sc is None:
                    continue
                if di and dj and (step(l, i + di, j) is None or step(l, i, j + dj) is None):
                    continue  # no squeezing diagonally between two blocked cells
                nxt = (l, ni, nj)
                ng = g + c + sc
                if ng < best.get(nxt, 1e18):
                    best[nxt] = ng
                    came[nxt] = cur
                    heapq.heappush(openh, (ng + h(*nxt), ng, nxt))
            ol = 1 - l
            vs = via_step(i, j)
            if vs is not None:
                sc = step(ol, i, j)
                if sc is not None:
                    nxt = (ol, i, j)
                    ng = g + VIA_COST + vs + sc
                    if ng < best.get(nxt, 1e18):
                        best[nxt] = ng
                        came[nxt] = cur
                        heapq.heappush(openh, (ng + h(*nxt), ng, nxt))
        return None

    def nets_near(self, l, x, y, r) -> Set[int]:
        """Nets of routed copper within r of a point on layer l (tracks on l, vias anywhere)."""
        out = set()
        for (n, tl, x1, y1, x2, y2, w) in self.tracks:
            if tl != l:
                continue
            dx, dy = x2 - x1, y2 - y1
            L = dx * dx + dy * dy
            t = 0.0 if L == 0 else max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / L))
            if math.hypot(x - (x1 + t * dx), y - (y1 + t * dy)) <= r + w / 2:
                out.add(n)
        for (n, vx, vy) in self.vias:
            if math.hypot(vx - x, vy - y) <= r + VIA_DIA / 2:
                out.add(n)
        return out

    def victims(self, net, cls, path) -> Set[int]:
        out = set()
        g0 = self.grid[cls]
        g2 = self.grid[2]
        prev = None
        for (l, i, j) in path:
            k = self.idx(i, j)
            v = g0[l][k]
            if v > 0 and v != net:
                out.add(v)
            elif v == MULTI and self.base[cls][l][k] == 0:
                x, y = self.pos(i, j)
                out |= self.nets_near(l, x, y, CLEARANCE + CLASSES[cls] + 0.05)
            if prev is not None and prev[0] != l:
                for ll in LAYERS:
                    v = g2[ll][k]
                    if v > 0 and v != net:
                        out.add(v)
                    elif v == MULTI and self.base[2][ll][k] == 0:
                        x, y = self.pos(i, j)
                        out |= self.nets_near(ll, x, y, CLEARANCE + CLASSES[2] + 0.05)
            prev = (l, i, j)
        out.discard(net)
        return out

    # ------------------------------------------------------------------ committing paths
    def add_via(self, net, x, y):
        self.vias.append((net, x, y))
        for l in LAYERS:
            self.mark_disc_all(l, x, y, VIA_DIA / 2, net)
            # no other via may overlap this one, whatever its net
            rr = VIA_DIA + 0.3
            i0, j0 = self.cell(x - rr, y - rr)
            i1, j1 = self.cell(x + rr, y + rr)
            self._mark(self.grid[2][l], i0, j0, i1, j1, MULTI,
                       lambda i, j: (i * PITCH - x) ** 2 + (j * PITCH - y) ** 2 <= rr * rr)

    def add_fixed_via(self, net, x, y):
        """A via that survives rebuilds (ground stitching)."""
        self.fixed_vias.append((net, x, y))
        self.add_via(net, x, y)

    def add_fixed_path(self, net, points, width):
        """A hand-prescribed route that survives rebuilds and rip-ups. `points` are
        (layer, x, y) in mm; consecutive points are joined by straight or 45-degree runs,
        a layer change at the same spot becomes a via."""
        cells: List[Cell] = []
        for (l1, x1, y1), (l2, x2, y2) in zip(points, points[1:]):
            i1, j1 = self.cell(x1, y1)
            i2, j2 = self.cell(x2, y2)
            if l1 != l2:
                if (i1, j1) != (i2, j2):
                    raise ValueError("layer change must stay in place")
                if not cells:
                    cells.append((l1, i1, j1))
                cells.append((l2, i2, j2))
                continue
            di, dj = i2 - i1, j2 - j1
            n = max(abs(di), abs(dj))
            if n and abs(di) not in (0, n) or abs(dj) not in (0, n):
                raise ValueError(f"run must be straight or diagonal: {(x1, y1)} -> {(x2, y2)}")
            si = (di > 0) - (di < 0)
            sj = (dj > 0) - (dj < 0)
            if not cells:
                cells.append((l1, i1, j1))
            for k in range(1, n + 1):
                cells.append((l1, i1 + si * k, j1 + sj * k))
        self.fixed_paths.setdefault(net, []).append((cells, width))
        self._mark_path(net, cells, width)
        return cells

    def _mark_path(self, net, path, width):
        runs: List[List[Cell]] = [[path[0]]]
        for a, b in zip(path, path[1:]):
            if a[0] != b[0]:
                x, y = self.pos(a[1], a[2])
                self.add_via(net, x, y)
                runs.append([b])
            else:
                runs[-1].append(b)
        for run in runs:
            if len(run) < 2:
                continue
            layer = run[0][0]
            simp = [run[0]]
            for k in range(1, len(run) - 1):
                d1 = (run[k][1] - run[k - 1][1], run[k][2] - run[k - 1][2])
                d2 = (run[k + 1][1] - run[k][1], run[k + 1][2] - run[k][2])
                if d1 != d2:
                    simp.append(run[k])
            simp.append(run[-1])
            for a, b in zip(simp, simp[1:]):
                x1, y1 = self.pos(a[1], a[2])
                x2, y2 = self.pos(b[1], b[2])
                self.tracks.append((net, layer, x1, y1, x2, y2, width))
            for (l, i, j) in run:
                x, y = self.pos(i, j)
                self.mark_disc_all(l, x, y, width / 2, net)

    def commit(self, net, path, width):
        self.committed.setdefault(net, []).append((path, width))
        self._mark_path(net, path, width)

    def unroute(self, net):
        self.committed.pop(net, None)
        self.rip_count[net] += 1
        self.rips += 1

    # ------------------------------------------------------------------ net routing
    def _find(self, net, cls, component, target):
        path = self.astar(net, cls, component, set(target.core), target.x, target.y)
        if path is not None:
            return path, cls
        if cls == 1:
            path = self.astar(net, 0, component, set(target.core), target.x, target.y)
            if path is not None:
                return path, 0
        return None, cls

    def route_net(self, net, pads: List[Pad], width, rippable: Optional[Set[int]] = None):
        """Returns (ok, fail, failed pads, nets ripped up on the way)."""
        if len(pads) < 2:
            return 0, 0, [], set()
        cls = 1 if width > SIG_W else 0
        remaining = list(pads)
        cx = sum(p.x for p in pads) / len(pads)
        cy = sum(p.y for p in pads) / len(pads)
        remaining.sort(key=lambda p: math.hypot(p.x - cx, p.y - cy))
        component: Set[Cell] = set()
        comp_pts = []
        for cells, _w in self.fixed_paths.get(net, []):
            component |= set(cells)
            comp_pts += [self.pos(i, j) for (_, i, j) in cells[::5]]
        if component:
            remaining = [p for p in remaining if not (p.core & component)]
        else:
            first = remaining.pop(0)
            component = set(first.core)
            comp_pts = [(first.x, first.y)]
        ok = fail = 0
        failed = []
        ripped: Set[int] = set()
        while remaining:
            remaining.sort(key=lambda p: min(math.hypot(p.x - qx, p.y - qy) for qx, qy in comp_pts))
            target = remaining.pop(0)
            path, used_cls = self._find(net, cls, component, target)
            if path is None and rippable:
                soft = {n for n in rippable if n != net and self.rip_count[n] < MAX_RIPS_PER_NET}
                for c in ([1, 0] if cls == 1 else [0]):
                    p = self.astar(net, c, component, set(target.core), target.x, target.y, soft=soft)
                    if p is None:
                        continue
                    vs = self.victims(net, c, p)
                    for v in vs:
                        self.unroute(v)
                    ripped |= vs          # whatever happens next, the victims must be routed again
                    self.rebuild()
                    if self.path_clean(net, c, p):
                        path, used_cls = p, c
                        self.log(f"   rip-up for {target}: nets {sorted(vs)}")
                    else:
                        self.log(f"   rip-up path for {target} not clean after rebuild; dropped")
                    break
            if path is None:
                self.log(f"   FAILED {target}")
                fail += 1
                failed.append(target)
                component |= set(target.core)
                comp_pts.append((target.x, target.y))
                continue
            self.commit(net, path, PWR_W if used_cls == 1 else SIG_W)
            component |= set(path) | set(target.core)
            comp_pts.append((target.x, target.y))
            comp_pts += [self.pos(i, j) for (_, i, j) in path[::6]]
            ok += 1
        if failed:
            self.failures[net] = failed
        else:
            self.failures.pop(net, None)
        return ok, fail, failed, ripped

    def route(self, nets: Dict[int, List[Pad]], order: List[int], names: Dict[int, str], rip_budget=80,
              final: Optional[List[int]] = None):
        """Route `order` with rip-up, then `final` (e.g. ground) without disturbing anything."""
        queue = list(order)
        routable = set(order)
        failures: Dict[int, List[Pad]] = {}
        for net in final or []:
            if net in routable:
                queue.remove(net)
                routable.discard(net)
        while queue:
            net = queue.pop(0)
            pads = nets.get(net, [])
            width = self.net_widths.get(net, SIG_W)
            rippable = routable if self.rips < rip_budget else None
            ok, fail, failed, ripped = self.route_net(net, pads, width, rippable)
            failures.pop(net, None)
            if failed:
                failures[net] = failed
            for v in sorted(ripped):
                if v not in queue:
                    queue.append(v)
            self.log(f"net {names.get(net, net):12s} pads={len(pads):2d} ok={ok:2d} fail={fail}"
                     + (f" (ripped {[names.get(v, v) for v in sorted(ripped)]})" if ripped else ""))
        for net in final or []:
            pads = nets.get(net, [])
            ok, fail, failed, _r = self.route_net(net, pads, self.net_widths.get(net, SIG_W), None)
            if failed:
                failures[net] = failed
            self.log(f"net {names.get(net, net):12s} pads={len(pads):2d} ok={ok:2d} fail={fail} (final)")
        total_ok = total_fail = 0
        for net in list(order) + list(final or []):
            n = len(nets.get(net, []))
            conns = n - 1 if n > 1 else 0
            f = len(failures.get(net, []))
            total_ok += conns - f
            total_fail += f
        self.log(f"rip-ups: {self.rips}")
        return total_ok, total_fail, failures

    # ------------------------------------------------------------------ diagnostics
    def reach_report(self, pad: Pad, cls=0, limit=3000):
        """How many cells a trace from this pad can reach, and which nets wall it in."""
        seen = set(pad.core)
        frontier = list(pad.core)
        blockers: Counter = Counter()
        while frontier and len(seen) < limit:
            l, i, j = frontier.pop()
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ni, nj = i + di, j + dj
                if ni < 0 or nj < 0 or ni >= self.nx or nj >= self.ny:
                    continue
                v = self.grid[cls][l][self.idx(ni, nj)]
                if v == 0 or v == pad.net:
                    if (l, ni, nj) not in seen:
                        seen.add((l, ni, nj))
                        frontier.append((l, ni, nj))
                else:
                    blockers[v] += 1
            if self.via_ok(i, j, pad.net) and self.free(cls, 1 - l, i, j, pad.net) and (1 - l, i, j) not in seen:
                seen.add((1 - l, i, j))
                frontier.append((1 - l, i, j))
        return len(seen), blockers.most_common(5)
