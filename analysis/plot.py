#!/usr/bin/env python3
"""Draw comparison graphs from analysis/results/summary.csv.

Usage: python3 analysis/plot.py        (needs: pip install matplotlib)
One grouped bar chart per metric: one group per input, one bar per algorithm.
"""
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")                       # no display needed
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
SUMMARY = os.path.join(HERE, "results", "summary.csv")
OUT_DIR = os.path.join(HERE, "graphs")

# Inputs where the algorithms behave identically or the scale dwarfs the rest
# (a 50000-unit job, a 1000-unit idle gap, single/identical patients).
SKIP_INPUTS = {"long_service", "idle_gap", "single", "identical",
               "lexicographic_ids"}

# (csv column, file name, y-axis label)
METRICS = [
    ("avg_waiting",     "avg_waiting.png",     "Average waiting time"),
    ("avg_turnaround",  "avg_turnaround.png",  "Average turnaround time"),
    ("avg_response",    "avg_response.png",    "Average response time"),
    ("utilization",     "utilization.png",     "Doctor utilization (%)"),
    ("preemptions",     "preemptions.png",     "Preemptions (context switches forced)"),
    ("max_ready_queue", "max_ready_queue.png", "Longest READY queue"),
]
ALGOS = ["FCFS", "PRIORITY", "PRIORITY_PREEMPTIVE"]


def load():
    with open(SUMMARY, newline="") as f:
        rows = [r for r in csv.DictReader(f) if r["input"] not in SKIP_INPUTS]
    inputs = []
    for r in rows:
        if r["input"] not in inputs:
            inputs.append(r["input"])
    return rows, inputs


def draw(rows, inputs, column, filename, label):
    width = 0.8 / len(ALGOS)
    fig, ax = plt.subplots(figsize=(max(7, 1.4 * len(inputs)), 4.5))
    for k, algo in enumerate(ALGOS):
        vals = [float(next(r[column] for r in rows
                           if r["input"] == i and r["algorithm"] == algo))
                for i in inputs]
        xs = [j + k * width for j in range(len(inputs))]
        ax.bar(xs, vals, width, label=algo)
    ax.set_xticks([j + width for j in range(len(inputs))])
    ax.set_xticklabels(inputs, rotation=30, ha="right")
    ax.set_ylabel(label)
    ax.set_title(label + " by algorithm")
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, filename), dpi=120)
    plt.close(fig)


def main():
    if not os.path.exists(SUMMARY):
        sys.exit("Run  sh analysis/compare.sh  first.")
    os.makedirs(OUT_DIR, exist_ok=True)
    rows, inputs = load()
    for column, filename, label in METRICS:
        draw(rows, inputs, column, filename, label)
        print("wrote graphs/" + filename)


if __name__ == "__main__":
    main()
