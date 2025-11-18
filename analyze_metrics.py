#!/usr/bin/env python3
import argparse, csv, glob, os, math, statistics
from collections import defaultdict

# ---- Team mapping (adjust if your topology changes) ----
GREEN_PROCS = {"B", "C"}
PINK_PROCS  = {"D", "E", "F"}

def proc_to_team(proc: str):
    if proc in GREEN_PROCS: return "green"
    if proc in PINK_PROCS:  return "pink"
    return None

# ---- CSV ingestion ----
EXPECTED_FIELDS = {
    "wall_ms","steady_ms","event","request_id","process","role","hostname",
    "pid","thread_id","queue_depth","active_count","chunk_number","records","extra"
}

def read_csvs(paths):
    for p in paths:
        if not os.path.isfile(p): 
            continue
        with open(p, newline="") as f:
            rdr = csv.DictReader(f)
            # tolerate files that miss some columns; we’ll guard access
            for r in rdr:
                yield r

def load_leader_rows(log_dir):
    # Prefer leader files; also include plain metrics.log if present
    paths = sorted(glob.glob(os.path.join(log_dir, "metrics-leader-*.csv")))
    # mlog  = os.path.join(log_dir, "metrics.log")
    # if os.path.isfile(mlog):
    #     paths.append(mlog)

    rows = []
    for r in read_csvs(paths):
        try:
            r["steady_ms"] = int(r.get("steady_ms","0"))
        except ValueError:
            try:
                r["steady_ms"] = int(float(r.get("steady_ms","0")))
            except Exception:
                continue
        r["event"] = (r.get("event") or "").strip()
        r["request_id"] = (r.get("request_id") or "").strip()
        # records may be empty/-1 for non-chunk events
        try:
            r["records"] = int(r.get("records","0"))
        except Exception:
            r["records"] = 0
        # Source process lives in 'extra' (e.g., "C", "E", "final from leader", etc.)
        extra = (r.get("extra") or "").strip().strip('"')
        tok   = extra.split(",")[0].split()[0] if extra else ""
        r["source_proc"] = tok  # like "B","C","D","E","F" or text
        rows.append(r)

    rows.sort(key=lambda x: x["steady_ms"])
    return rows

# ---- Helpers ----
def jain2(xg, xp):
    if xg==0 and xp==0: return None
    denom = xg*xg + xp*xp
    if denom == 0: return 1.0
    return ((xg + xp)**2) / (2.0 * denom)

def percentile(values, p):
    if not values: return None
    vs = sorted(values)
    k = (len(vs)-1) * (p/100.0)
    f = math.floor(k); c = math.ceil(k)
    if f == c: return vs[int(k)]
    return vs[f] + (vs[c]-vs[f]) * (k-f)

def describe(name, values, unit="ms"):
    if not values:
        print(f"{name}: (no data)")
        return
    vals = list(values)
    print(f"{name}: n={len(vals)} {unit}")
    print(f"  p50={statistics.median(vals):.1f}  p95={percentile(vals,95):.1f}  min={min(vals):.1f}  max={max(vals):.1f}")

# ---- Metrics ----
def compute_ttfc(rows):
    """Time to first CHUNK_RELAY from each team since START_DELEGATE per request."""
    start = {}
    first = defaultdict(dict)  # req -> {team: t_first}
    for r in rows:
        ev = r["event"]
        if ev == "START_DELEGATE":
            start[r["request_id"]] = r["steady_ms"]
        elif ev == "CHUNK_RELAY":
            team = proc_to_team(r["source_proc"])
            if not team: continue
            d = first[r["request_id"]]
            if team not in d:
                d[team] = r["steady_ms"]
    deltas = {"green": [], "pink": []}
    for req, d in first.items():
        t0 = start.get(req)
        if t0 is None: 
            continue
        for team, t1 in d.items():
            deltas[team].append(t1 - t0)
    return deltas

def compute_run_lengths_coactive(rows):
    """
    Max consecutive CHUNK_RELAY streak for the same TEAM per request, after both teams have emitted at least one chunk.
    """
    seq_by_req = defaultdict(list)  # req -> [(t, team)]
    seen_team  = defaultdict(lambda: {"green": False, "pink": False})

    for r in rows:
        if r["event"] != "CHUNK_RELAY": 
            continue
        team = proc_to_team(r["source_proc"])
        if not team: 
            continue
        req = r["request_id"]
        seq_by_req[req].append( (r["steady_ms"], team) )
        seen_team[req][team] = True

    out = []
    for req, seq in seq_by_req.items():
        if not (seen_team[req]["green"] and seen_team[req]["pink"]):
            continue
        seq.sort()
        # begin once both teams have appeared at least once
        seen = {"green": False, "pink": False}
        start_i = None
        for i,(t,team) in enumerate(seq):
            seen[team] = True
            if seen["green"] and seen["pink"]:
                start_i = i
                break
        if start_i is None:
            continue
        cur_team = None
        cur_run = 0
        max_run = 0
        for i in range(start_i, len(seq)):
            _, team = seq[i]
            if team == cur_team:
                cur_run += 1
            else:
                cur_team = team
                cur_run = 1
            if cur_run > max_run:
                max_run = cur_run
        out.append(max_run)
    return out

def compute_team_jain_per_window(rows, window_ms=1000, use_records=True):
    buckets = defaultdict(lambda: {"green":0, "pink":0})
    for r in rows:
        if r["event"] != "CHUNK_RELAY": 
            continue
        team = proc_to_team(r["source_proc"])
        if not team: continue
        w = r["steady_ms"] // window_ms
        val = r["records"] if (use_records and r["records"]>0) else 1
        buckets[w][team] += val
    jains = []
    for _, d in sorted(buckets.items()):
        g, p = d["green"], d["pink"]
        if g>0 and p>0:
            j = jain2(g,p)
            if j is not None:
                jains.append(j)
    return jains

def compute_e2e_and_size(rows):
    enqueue, finish = {}, {}
    size_by_req = defaultdict(int)
    for r in rows:
        req = r["request_id"]
        if r["event"] == "ENQUEUE":
            enqueue[req] = r["steady_ms"]
        elif r["event"] == "FINISH":
            finish[req] = r["steady_ms"]
        elif r["event"] == "CHUNK_RELAY":
            size_by_req[req] += max(0, r["records"])
    e2e = {}
    for req,t0 in enqueue.items():
        t1 = finish.get(req)
        if t1 is not None:
            e2e[req] = t1 - t0
    return e2e, size_by_req

def compute_chunk_gaps(rows):
    ts_by_req = defaultdict(list)
    for r in rows:
        if r["event"] == "CHUNK_RELAY":
            ts_by_req[r["request_id"]].append(r["steady_ms"])
    gaps = []
    for req, ts in ts_by_req.items():
        ts.sort()
        for i in range(1, len(ts)):
            gaps.append(ts[i]-ts[i-1])
    return gaps

def compute_team_completion_dispersion(rows):
    last_by_req = defaultdict(lambda: {"green":None, "pink":None})
    for r in rows:
        if r["event"] != "CHUNK_RELAY":
            continue
        team = proc_to_team(r["source_proc"])
        if not team: continue
        last_by_req[r["request_id"]][team] = r["steady_ms"]
    out = []
    for req, d in last_by_req.items():
        g, p = d["green"], d["pink"]
        if g is not None and p is not None:
            out.append(abs(g - p))
    return out

# ---- Main ----
def main():
    ap = argparse.ArgumentParser(description="Analyze leader metrics for feeder-queues + RR multiplexer.")
    ap.add_argument("log_dir", nargs="?", default="./logs", help="Directory containing metrics-leader-*.csv (and/or metrics.log)")
    ap.add_argument("--window-ms", type=int, default=1000, help="Jain window (ms), default 1000")
    ap.add_argument("--chunks-not-records", action="store_true",
                    help="Compute Jain using chunk counts instead of records")
    args = ap.parse_args()

    rows = load_leader_rows(args.log_dir)
    if not rows:
        print("No leader metrics found in", args.log_dir)
        return

    # 1) TTFC per team
    ttfc = compute_ttfc(rows)
    print("\n== TTFC since START_DELEGATE ==")
    describe("TTFC-green", ttfc.get("green", []))
    describe("TTFC-pink",  ttfc.get("pink",  []))

    # 2) Interleaving run length (co-active)
    runs = compute_run_lengths_coactive(rows)
    print("\n== Interleaving run length (co-active) ==")
    describe("Run length", runs, "chunks")

    # 3) Team-level Jain per window
    jains = compute_team_jain_per_window(rows, args.window_ms, use_records=not args.chunks_not_records)
    print(f"\n== Team Jain over {args.window_ms} ms windows (co-active only) ==")
    if jains:
        print(f"  mean={statistics.mean(jains):.3f}  p50={statistics.median(jains):.3f}  p95={percentile(jains,95):.3f}  min={min(jains):.3f}")
        poor = sum(1 for j in jains if j < 0.85)
        print(f"  windows={len(jains)}  Jain<0.85: {poor} ({100.0*poor/len(jains):.1f}%)")
    else:
        print("  (no co-active windows)")

    # 4) E2E latency split by request size
    e2e, size = compute_e2e_and_size(rows)
    sizes = [v for v in size.values()]
    if sizes:
        med = statistics.median(sizes)
        small = [e2e[req] for req,val in size.items() if req in e2e and val <= med]
        large = [e2e[req] for req,val in size.items() if req in e2e and val >  med]
        print("\n== End-to-end latency by request size (median split on records) ==")
        describe("Small E2E", small)
        describe("Large E2E", large)
    else:
        print("\n== End-to-end latency by request size ==")
        print("  (no size data)")

    # 5) Chunk gap
    gaps = compute_chunk_gaps(rows)
    print("\n== Per-request chunk gap (Δ between consecutive CHUNK_RELAY) ==")
    describe("Chunk gap", gaps)

    # 6) Team completion dispersion
    disp = compute_team_completion_dispersion(rows)
    print("\n== Team completion dispersion (|last Green - last Pink|) ==")
    describe("Completion dispersion", disp)

if __name__ == "__main__":
    main()
