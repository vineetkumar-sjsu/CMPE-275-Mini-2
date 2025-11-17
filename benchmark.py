#!/usr/bin/env python3
"""
Benchmark a fire_client command N times and auto-discover server PIDs to track memory.

- No need to pass PIDs. We discover them by matching process names/cmdlines:
  ["leader_server", "team_leader_server", "worker_server", "worker_server.py"]

- Reports per-run latency, client max RSS, and TOTAL server max RSS (sum across all matched PIDs).
  At the end, prints a per-process max RSS across all runs.

Usage:
  python bench_fire_client.py \
    --cmd "./build/fire_client localhost:50051 --start 20200810 --end 20200924 --chunk 500" \
    -n 10

Options:
  --server-pattern can be repeated to override the default patterns.
"""

import argparse
import os
import shlex
import subprocess
import sys
import time

try:
    import psutil
    PSUTIL_OK = True
except Exception:
    PSUTIL_OK = False

DEFAULT_PATTERNS = [
    "leader_server",
    "team_leader_server",
    "worker_server",
    "worker_server.py",
]

# -----------------------------
# Discovery
# -----------------------------
def discover_server_pids(patterns):
    """Return (pids, meta) where meta is {pid: {'name': str, 'cmd': str}}."""
    pids = []
    meta = {}
    if PSUTIL_OK:
        for p in psutil.process_iter(attrs=["pid", "name", "cmdline"]):
            try:
                name = p.info.get("name") or ""
                cmdline = p.info.get("cmdline") or []
                cmdtxt = " ".join(cmdline)
                if any((pat in name) or (pat and pat in cmdtxt) for pat in patterns):
                    pids.append(p.info["pid"])
                    meta[p.info["pid"]] = {"name": name, "cmd": cmdtxt}
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
    else:
        # Fallback to pgrep if psutil not available
        try:
            pat = "|".join(patterns)
            out = subprocess.check_output(["pgrep", "-f", pat], text=True).strip()
            for line in out.splitlines():
                if not line.strip():
                    continue
                pid = int(line.strip())
                pids.append(pid)
                # Best-effort name lookup
                try:
                    cmd = subprocess.check_output(["ps", "-p", str(pid), "-o", "command="], text=True).strip()
                except Exception:
                    cmd = ""
                meta[pid] = {"name": os.path.basename(cmd.split()[0]) if cmd else "", "cmd": cmd}
        except Exception:
            pass
    # De-dup and stable order
    pids = sorted(set(pids))
    return pids, meta

# -----------------------------
# Memory sampling helpers
# -----------------------------
def sample_rss_sum_kb(pids):
    """Sum RSS of all given PIDs (in KB). Ignores missing/dead processes."""
    total = 0
    if not PSUTIL_OK:
        return total
    for pid in pids:
        try:
            p = psutil.Process(pid)
            total += p.memory_info().rss
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return int((total + 1023) // 1024)

def sample_rss_per_pid_kb(pids):
    """Return {pid: rss_kb} for existing PIDs."""
    out = {}
    if not PSUTIL_OK:
        return out
    for pid in pids:
        try:
            p = psutil.Process(pid)
            out[pid] = int((p.memory_info().rss + 1023) // 1024)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return out

def sample_proc_rss_kb(pid):
    """Return RSS of a single process (in KB), 0 if missing."""
    if not PSUTIL_OK:
        return 0
    try:
        return int((psutil.Process(pid).memory_info().rss + 1023) // 1024)
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return 0

# -----------------------------
# Runner
# -----------------------------
def run_once(cmd: str, server_pids, sample_interval: float = 0.1):
    """
    Runs `cmd` once.
    Returns (elapsed_ms, client_rss_max_kb, total_server_rss_max_kb, per_pid_max_kb, returncode)
    """
    proc = subprocess.Popen(shlex.split(cmd), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    t0 = time.perf_counter()
    client_max = 0
    server_total_max = 0
    per_pid_max = {}

    # Prefetch psutil.Process for client if available
    client_ps = psutil.Process(proc.pid) if PSUTIL_OK else None

    while True:
        ret = proc.poll()

        # client rss
        if PSUTIL_OK and client_ps is not None:
            try:
                rss = int((client_ps.memory_info().rss + 1023) // 1024)
                if rss > client_max:
                    client_max = rss
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass

        # server rss
        cur_total = sample_rss_sum_kb(server_pids)
        if cur_total > server_total_max:
            server_total_max = cur_total

        # per-pid peaks
        cur_map = sample_rss_per_pid_kb(server_pids)
        for pid, rss_kb in cur_map.items():
            old = per_pid_max.get(pid, 0)
            if rss_kb > old:
                per_pid_max[pid] = rss_kb

        if ret is not None:
            break
        time.sleep(sample_interval)

    t1 = time.perf_counter()
    elapsed_ms = int(round((t1 - t0) * 1000))
    return elapsed_ms, client_max, server_total_max, per_pid_max, proc.returncode

# -----------------------------
# Main
# -----------------------------
def main():
    ap = argparse.ArgumentParser(description="Benchmark fire_client and auto-track server memory.")
    ap.add_argument("--cmd", default="./build/fire_client localhost:50051 --start 20200810 --end 20200924 --chunk 500",
                    help="Command to run per iteration.")
    ap.add_argument("-n", "--iterations", type=int, default=10, help="Number of measured runs.")
    ap.add_argument("--warmup", type=int, default=1, help="Warm-up runs (not measured).")
    ap.add_argument("--sleep", type=float, default=0.2, help="Seconds to sleep between runs.")
    ap.add_argument("--sample-interval", type=float, default=0.1, help="Seconds between RSS samples.")
    ap.add_argument("--warn-ms", type=int, default=100_000, help="Warn if latency exceeds this (ms).")
    ap.add_argument("--server-pattern", action="append",
                    help="Add a server pattern to match (can repeat). Defaults cover leader/tl/worker/python worker.")
    ap.add_argument("--no-server-mem", action="store_true",
                    help="Skip server memory tracking (client-only).")
    args = ap.parse_args()

    patterns = args.server_pattern if args.server_pattern else DEFAULT_PATTERNS

    print(f"Command: {args.cmd}")
    print(f"Iterations: {args.iterations}")
    print(f"psutil: {'enabled' if PSUTIL_OK else 'NOT available (memory=0 KB)'}")

    server_pids = []
    server_meta = {}

    if not args.no_server_mem:
        server_pids, server_meta = discover_server_pids(patterns)
        if server_pids:
            print(f"Tracking server PIDs: {server_pids}")
            for pid in server_pids:
                meta = server_meta.get(pid, {})
                nm = meta.get("name", "")
                cmd = meta.get("cmd", "")
                shown = nm if nm else (cmd[:60] + ("..." if len(cmd) > 60 else ""))
                print(f"  PID {pid}: {shown}")
        else:
            print("WARNING: No server PIDs discovered. TotalServerRSS will be 0 KB.")
    print()


    # Warm-up (not measured)
    for _ in range(max(0, args.warmup)):
        try:
            run_once(args.cmd, server_pids, args.sample_interval)
        except Exception:
            pass

    times = []
    client_rsses = []
    server_totals = []
    per_pid_peak_across_runs = {}  # {pid: max_kb}

    over = 0
    for i in range(1, args.iterations + 1):
        tms, cl_kb, sv_kb, per_pid_map, rc = run_once(args.cmd, server_pids, args.sample_interval)
        times.append(tms)
        client_rsses.append(cl_kb)
        server_totals.append(sv_kb)
        for pid, kb in per_pid_map.items():
            old = per_pid_peak_across_runs.get(pid, 0)
            if kb > old:
                per_pid_peak_across_runs[pid] = kb

        warn = f"  [> {args.warn_ms} ms]" if tms > args.warn_ms else ""
        print(f"Run {i:2d}: {tms:8d} ms   ClientRSS_max:{cl_kb:10d} KB   TotalServerRSS_max:{sv_kb:10d} KB{warn}")
        if tms > args.warn_ms:
            over += 1
        if rc != 0:
            print(f"  (note) process exit code: {rc}", file=sys.stderr)
        time.sleep(max(0.0, args.sleep))

    def stats(arr):
        return (min(arr), sum(arr)//len(arr), max(arr)) if arr else (0, 0, 0)

    t_min, t_avg, t_max = stats(times)
    c_min, c_avg, c_max = stats(client_rsses)
    s_min, s_avg, s_max = stats(server_totals)

    print("-----------------------------")
    print(f"Latency (ms):               min={t_min}   avg={t_avg}   max={t_max}")
    print(f"Client Max RSS (KB):        min={c_min}   avg={c_avg}   max={c_max}")
    if not args.no_server_mem:
        print(f"Total Server Max RSS (KB):  min={s_min}   avg={s_avg}   max={s_max}")
        if per_pid_peak_across_runs:
            print("Per-process Max RSS (KB) across runs:")
            for pid in sorted(per_pid_peak_across_runs):
                kb = per_pid_peak_across_runs[pid]
                meta = server_meta.get(pid, {})
                nm = meta.get("name", "")
                print(f"  PID {pid}: {kb:8d} KB{('  ' + nm) if nm else ''}")

    if over:
        print(f"\nNote: {over} run(s) exceeded {args.warn_ms} ms.")

if __name__ == "__main__":
    os.environ.setdefault("PYTHONUNBUFFERED", "1")
    main()
