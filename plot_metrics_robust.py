"""
Построение графиков по CSV из приложения (устойчивый к путям).
- Ищет файлы сначала в текущей директории, затем в ./build
- Можно явно указать пути: python3 plot_metrics.py --scal build/scalability.csv --res build/results.csv
Требует matplotlib. Не использует seaborn.
"""
import csv
import os
import argparse
from collections import defaultdict
import matplotlib.pyplot as plt

def resolve_path(default_name, cli_value):
    """
    Возвращает путь к файлу по приоритету:
      1) явно переданный аргументом CLI,
      2) файл в текущей директории,
      3) файл в ./build/
    Бросит FileNotFoundError, если не найден.
    """
    if cli_value:
        if os.path.exists(cli_value):
            return cli_value
        raise FileNotFoundError(f"Не найден файл по указанному пути: {cli_value}")
    if os.path.exists(default_name):
        return default_name
    alt = os.path.join("build", default_name)
    if os.path.exists(alt):
        return alt
    raise FileNotFoundError(f"Не найден {default_name} ни в текущей директории, ни в ./build/")

def read_scalability(path):
    rows = []
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        for d in r:
            d["size"] = int(d["size"])
            d["elapsed_ns"] = float(d["elapsed_ns"])
            d["avg_ns"] = float(d["avg_ns"])
            d["ops_per_sec"] = float(d["ops_per_sec"])
            d["hit_rate"] = float(d["hit_rate"])
            rows.append(d)
    return rows

def read_results(path):
    rows = []
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        for d in r:
            d["capacity"] = int(d["capacity"])
            d["size"] = int(d["size"])
            d["total_ops"] = int(d["total_ops"])
            d["gets"] = int(d["gets"])
            d["puts"] = int(d["puts"])
            d["evictions"] = int(d["evictions"])
            d["hit_rate"] = float(d["hit_rate"])
            d["miss_rate"] = float(d["miss_rate"])
            d["avg_ns"] = float(d["avg_ns"])
            d["ops_per_sec"] = float(d["ops_per_sec"])
            d["elapsed_ns"] = float(d["elapsed_ns"])
            rows.append(d)
    return rows

def plot_scalability(rows):
    grouped = defaultdict(list)
    for d in rows:
        key = f'{d["algo"]}-{d["impl"]}'
        grouped[key].append((d["size"], d["elapsed_ns"]))
    for k in grouped:
        grouped[k].sort()

    plt.figure()
    for k, arr in grouped.items():
        xs = [x for x,_ in arr]
        ys = [y for _,y in arr]
        plt.plot(xs, ys, marker="o", label=k)
    plt.xlabel("Ёмкость кэша (size)")
    plt.ylabel("Время выполнения, нс")
    plt.title("Масштабируемость (время vs размер)")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.savefig("scalability_time.png", dpi=150)

def plot_hit_rate(rows):
    grouped = defaultdict(list)
    for d in rows:
        key = f'{d["algo"]}-{d["impl"]}'
        grouped[key].append((d["size"], d["hit_rate"]))
    for k in grouped:
        grouped[k].sort()

    plt.figure()
    for k, arr in grouped.items():
        xs = [x for x,_ in arr]
        ys = [y for _,y in arr]
        plt.plot(xs, ys, marker="o", label=k)
    plt.xlabel("Ёмкость кэша (size)")
    plt.ylabel("Hit Rate, %")
    plt.title("Качество кэширования (Hit Rate) vs размер")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.savefig("scalability_hit_rate.png", dpi=150)

def plot_ops(results):
    labels, values = [], []
    for d in results:
        labels.append(f'{d["algo"]}-{d["impl"]}')
        values.append(d["ops_per_sec"])

    plt.figure()
    plt.bar(labels, values)
    plt.ylabel("Операций/сек")
    plt.title("Производительность реализаций (итоговый прогон)")
    plt.tight_layout()
    plt.savefig("results_ops.png", dpi=150)

def main():
    parser = argparse.ArgumentParser(description="Построение графиков по CSV из кэш-эмулятора")
    parser.add_argument("--scal", "--scalability", dest="scal", default=None, help="Путь к scalability.csv")
    parser.add_argument("--res", "--results", dest="res", default=None, help="Путь к results.csv")
    args = parser.parse_args()

    scal_path = resolve_path("scalability.csv", args.scal)
    res_path = resolve_path("results.csv", args.res)
    print(f"Читаю: {scal_path} и {res_path}")

    scal = read_scalability(scal_path)
    res = read_results(res_path)
    plot_scalability(scal)
    plot_hit_rate(scal)
    plot_ops(res)
    print("Сохранены графики: scalability_time.png, scalability_hit_rate.png, results_ops.png")

if __name__ == "__main__":
    main()
