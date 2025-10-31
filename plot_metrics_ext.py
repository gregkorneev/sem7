"""
Графики по расширенным CSV (matplotlib без seaborn).
"""
import csv, os, argparse
from collections import defaultdict
import matplotlib.pyplot as plt

def read_csv(path):
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        return list(r)

def to_float(d, key, default=0.0):
    try:
        return float(d.get(key, default))
    except Exception:
        return default

def resolve_path(name, cli):
    if cli and os.path.exists(cli): return cli
    if os.path.exists(name): return name
    alt = os.path.join("build", name)
    if os.path.exists(alt): return alt
    raise FileNotFoundError(f"Не найден {name}")

def lineplot(x, ys, labels, title, xlabel, ylabel, outfile):
    plt.figure()
    for y, label in zip(ys, labels):
        plt.plot(x, y, marker="o", label=label)
    plt.title(title)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig(outfile, dpi=150)

def barplot(labels, values, title, ylabel, outfile):
    plt.figure()
    plt.bar(labels, values)
    plt.title(title)
    plt.ylabel(ylabel)
    plt.tight_layout()
    plt.savefig(outfile, dpi=150)

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default=None)
    ap.add_argument("--scal", default=None)
    ap.add_argument("--stability", default=None)
    ap.add_argument("--eff", default=None)
    ap.add_argument("--roi", default=None)
    args = ap.parse_args()

    results = read_csv(resolve_path("results_extended.csv", args.results))
    scal = read_csv(resolve_path("scalability_extended.csv", args.scal))
    stab = read_csv(resolve_path("stability.csv", args.stability))
    eff  = read_csv(resolve_path("efficiency_score.csv", args.eff))
    roi  = read_csv(resolve_path("roi.csv", args.roi))

    groups = defaultdict(list)
    for d in scal:
        key = f'{d["algo"]}-{d["impl"]}'
        groups[key].append((int(d["size"]), to_float(d,"elapsed_ns")))
    for k in groups: groups[k].sort()
    xs = [s for s,_ in groups["LRU-iter"]]
    ys = [ [v for _,v in groups.get(name, [])] for name in ["LRU-iter","LRU-rec","LFU-iter","LFU-rec"] ]
    lineplot(xs, ys, ["LRU-iter","LRU-rec","LFU-iter","LFU-rec"],
             "Масштабируемость: Время vs Размер", "Размер кэша", "Время (нс)",
             "scalability_time_ext.png")

    groups_hr = defaultdict(list)
    for d in scal:
        key = f'{d["algo"]}-{d["impl"]}'
        groups_hr[key].append((int(d["size"]), to_float(d,"hit_rate")))
    for k in groups_hr: groups_hr[k].sort()
    xs2 = [s for s,_ in groups_hr["LRU-iter"]]
    ys2 = [ [v for _,v in groups_hr.get(name, [])] for name in ["LRU-iter","LRU-rec","LFU-iter","LFU-rec"] ]
    lineplot(xs2, ys2, ["LRU-iter","LRU-rec","LFU-iter","LFU-rec"],
             "Качество кэширования: Hit Rate vs Размер", "Размер кэша", "Hit Rate (%)",
             "scalability_hit_ext.png")

    labels = [ f'{d["algo"]}-{d["impl"]}' for d in results ]
    values = [ to_float(d,"eviction_efficiency") for d in results ]
    barplot(labels, values, "Эффективность вытеснений (useful %)", "Доля полезных вытеснений, %",
            "eviction_eff_bar.png")

    labels2 = [ f'{d["algo"]}-{d["impl"]}' for d in eff ]
    values2 = [ to_float(d,"score") for d in eff ]
    barplot(labels2, values2, "Общая оценка эффективности", "Score (0..1+)", "efficiency_score.png")

    labels3 = [ f'{d["algo"]}-{d["impl"]}' for d in roi ]
    values3 = [ to_float(d,"roi") for d in roi ]
    barplot(labels3, values3, "Экономическая эффективность (ROI)", "ROI (условные ед.)", "roi_bar.png")

    print("Сохранены графики:")
    print(" - scalability_time_ext.png")
    print(" - scalability_hit_ext.png")
    print(" - eviction_eff_bar.png")
    print(" - efficiency_score.png")
    print(" - roi_bar.png")

if __name__ == "__main__":
    main()
