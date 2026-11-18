#!/usr/bin/env python3
"""Independent reference model used to produce tests/expected/*.out.

It deliberately works differently from the C code: it advances ONE time unit
at a time (no event jumping, no heap) and follows spec Sections 7-10 literally.
Usage: python3 tests/reference_sim.py INPUT ALGORITHM
"""
import sys

DOCTORS = 2

def load(path):
    rows = [l.strip() for l in open(path) if l.strip() and not l.startswith('#')]
    n, _q = int(rows[0]), int(rows[1])
    ps = []
    for r in rows[2:]:
        i, a, p, s, c = [x.strip() for x in r.split(',')]
        ps.append(dict(id=i, arr=int(a), pri=int(p), svc=int(s),
                       rem=int(s), start=-1, end=-1))
    assert len(ps) == n
    return ps

def key(p):                     # best first: priority, arrival, id
    return (p['pri'], p['arr'], p['id'])

def simulate(ps, algo):
    t, done = 0, 0
    running = [None] * DOCTORS
    ready = []
    arrived = set()
    while done < len(ps):
        # arrivals at time t join READY
        for p in ps:
            if p['arr'] <= t and p['id'] not in arrived:
                arrived.add(p['id']); ready.append(p)
        # fill idle doctors with the best ready patient
        best_key = (lambda p: (p['arr'], p['id'])) if algo == 'FCFS' else key
        for d in range(DOCTORS):
            if running[d] is None and ready:
                ready.sort(key=best_key)
                p = ready.pop(0); running[d] = p
                if p['start'] < 0: p['start'] = t
        # preemption: strictly higher priority than at least one running patient
        if algo == 'PRIORITY_PREEMPTIVE':
            while ready:
                ready.sort(key=key)
                w = ready[0]
                busy = [d for d in range(DOCTORS) if running[d]]
                if not busy: break
                # victim = lowest priority (largest number), then later arrival,
                # then lexicographically larger id
                v = max(busy, key=lambda d: (running[d]['pri'], running[d]['arr'], running[d]['id']))
                if w['pri'] >= running[v]['pri']: break
                ready.append(running[v]); ready.pop(0)
                running[v] = w
                if w['start'] < 0: w['start'] = t
        # run one time unit
        for d in range(DOCTORS):
            p = running[d]
            if p:
                p['rem'] -= 1
                if p['rem'] == 0:
                    p['end'] = t + 1; running[d] = None; done += 1
        t += 1

def report(ps):
    out = ["PROCESS_ID,ARRIVAL,PRIORITY,SERVICE,START,COMPLETION,WAITING,TURNAROUND,RESPONSE"]
    tw = tt = tr = busy = 0
    for p in sorted(ps, key=lambda p: p['id']):
        tat = p['end'] - p['arr']; wt = tat - p['svc']; rt = p['start'] - p['arr']
        out.append(f"{p['id']},{p['arr']},{p['pri']},{p['svc']},{p['start']},{p['end']},{wt},{tat},{rt}")
        tw += wt; tt += tat; tr += rt; busy += p['svc']
    n = len(ps)
    span = max(p['end'] for p in ps) - min(p['arr'] for p in ps)
    out += ["SUMMARY", f"AVERAGE_WAITING,{tw/n:.2f}", f"AVERAGE_TURNAROUND,{tt/n:.2f}",
            f"AVERAGE_RESPONSE,{tr/n:.2f}", f"THROUGHPUT,{n/span:.4f}",
            f"UTILIZATION,{busy/(DOCTORS*span)*100:.2f}"]
    return "\n".join(out) + "\n"

if __name__ == '__main__':
    ps = load(sys.argv[1]); simulate(ps, sys.argv[2]); sys.stdout.write(report(ps))
