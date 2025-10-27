#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <memory>
#include <iomanip>
#include <fstream>
#include <string>
#include "CacheBase.h"
#include "LRU.h"
#include "LFU.h"

using Clock = std::chrono::high_resolution_clock;
using Ns = std::chrono::nanoseconds;

// Генерация рабочей нагрузки: часть повторяющихся "горячих" ключей + случайные
std::vector<int> makeWorkload(int total_ops, int universe, double locality = 0.7) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> uni(0, universe - 1);
    std::uniform_int_distribution<int> hot(0, std::max(1, universe/10) - 1);
    std::vector<int> req;
    req.reserve(total_ops);
    for (int i = 0; i < total_ops; ++i) {
        if (uni(rng) / (double)universe < locality) req.push_back(hot(rng)); // "горячие" ключи
        else req.push_back(uni(rng));
    }
    return req;
}

// Прогон сценария: лёгкий прогрев, затем смесь get/put
long long runScenario(ICache& cache, const std::vector<int>& ops) {
    auto t0 = Clock::now();
    // прогрев: наполним половину ёмкости
    for (int k = 0; k < (int)cache.capacity()/2; ++k) cache.put(k, k*10);
    for (int x : ops) {
        // 70% get, 30% put
        if (x % 10 < 7) (void)cache.get(x);
        else cache.put(x, x*10);
    }
    auto t1 = Clock::now();
    return std::chrono::duration_cast<Ns>(t1 - t0).count();
}

// Локализованный вывод метрик и запись в CSV
void printMetrics(const char* name, const char* algo, const char* impl,
                  const ICache& c, long long elapsed_ns, int total_ops, std::ofstream& csv) {
    const auto& cnt = c.counters();
    double hitRate = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
    double missRate = 100.0 - hitRate;
    double avgTimeNs = total_ops ? (double)elapsed_ns / total_ops : 0.0;
    double opsPerSec = elapsed_ns ? (double)total_ops / (elapsed_ns / 1e9) : 0.0;

    std::cout << "\n[" << name << "]\n";
    std::cout << "Ёмкость кэша: " << c.capacity() << ", Текущий размер: " << c.size() << "\n";
    std::cout << "Всего операций: " << total_ops
              << " (чтений: " << cnt.gets << ", записей: " << cnt.puts
              << ", вытеснений: " << cnt.evictions << ")\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Процент попаданий (Hit Rate): " << hitRate << "%\n";
    std::cout << "Процент промахов (Miss Rate): " << missRate << "%\n";
    std::cout << "Среднее время одной операции: " << avgTimeNs << " нс\n";
    std::cout << "Производительность: " << opsPerSec << " операций/сек\n";

    // CSV: algo,impl,capacity,size,total_ops,gets,puts,evictions,hit_rate,miss_rate,avg_ns,ops_per_sec,elapsed_ns
    if (csv) {
        csv << algo << "," << impl << "," << c.capacity() << "," << c.size() << ","
            << total_ops << "," << cnt.gets << "," << cnt.puts << "," << cnt.evictions << ","
            << std::fixed << std::setprecision(4) << hitRate << "," << missRate << ","
            << avgTimeNs << "," << opsPerSec << "," << elapsed_ns << "\n";
    }
}

int main() {
    // Параметры базового прогона
    const int capacity = 128;         // ёмкость кэша
    const int total_ops = 20000;      // операций
    const int universe = 2000;        // диапазон ключей
    auto ops = makeWorkload(total_ops, universe, 0.75);

    // CSV для суммарных метрик
    std::ofstream csv("results.csv");
    csv << "algo,impl,capacity,size,total_ops,gets,puts,evictions,hit_rate,miss_rate,avg_ns,ops_per_sec,elapsed_ns\n";

    // 4 реализации
    LRUCacheIter lru_it(capacity);
    LRUCacheRec  lru_rec(capacity);
    LFUCacheIter lfu_it(capacity);
    LFUCacheRec  lfu_rec(capacity);

    long long t_lru_it = runScenario(lru_it, ops);
    long long t_lru_rec = runScenario(lru_rec, ops);
    long long t_lfu_it = runScenario(lfu_it, ops);
    long long t_lfu_rec = runScenario(lfu_rec, ops);

    printMetrics("LRU (итеративный)", "LRU", "iter", lru_it, t_lru_it, total_ops + capacity/2, csv);
    printMetrics("LRU (рекурсивный)", "LRU", "rec",  lru_rec, t_lru_rec, total_ops + capacity/2, csv);
    printMetrics("LFU (итеративный)", "LFU", "iter", lfu_it, t_lfu_it, total_ops + capacity/2, csv);
    printMetrics("LFU (рекурсивный)", "LFU", "rec",  lfu_rec, t_lfu_rec, total_ops + capacity/2, csv);
    csv.close();

    // Сравнение (Speedup Ratio)
    std::cout << "\n===== Сравнение производительности =====\n";
    std::cout << "(>1.0 — числитель медленнее, <1.0 — быстрее)\n";
    std::cout << "Отношение рекурсивного к итеративному (LRU): "
              << (double)t_lru_rec / (double)t_lru_it << "\n";
    std::cout << "Отношение рекурсивного к итеративному (LFU): "
              << (double)t_lfu_rec / (double)t_lfu_it << "\n";
    std::cout << "Отношение LRU/LFU (итеративных): "
              << (double)t_lru_it / (double)t_lfu_it << "\n";

    // ===== Тест масштабируемости (Time Complexity by Size) =====
    std::vector<int> sizes = {16, 32, 64, 128, 256, 512, 1024};
    std::ofstream scsv("scalability.csv");
    scsv << "size,algo,impl,elapsed_ns,avg_ns,ops_per_sec,hit_rate\n";

    for (int cap : sizes) {
        auto workload = makeWorkload(15000, 4000, 0.75);

        // ИТЕРАЦИОННЫЕ
        {
            LRUCacheIter c(cap);
            auto t = runScenario(c, workload);
            const auto& cnt = c.counters();
            double hitRate = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
            double avg = (double)t / (workload.size() + cap/2);
            double opsps = (double)(workload.size() + cap/2) / (t / 1e9);
            scsv << cap << ",LRU,iter," << t << "," << avg << "," << opsps << "," << hitRate << "\n";
        }
        {
            LFUCacheIter c(cap);
            auto t = runScenario(c, workload);
            const auto& cnt = c.counters();
            double hitRate = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
            double avg = (double)t / (workload.size() + cap/2);
            double opsps = (double)(workload.size() + cap/2) / (t / 1e9);
            scsv << cap << ",LFU,iter," << t << "," << avg << "," << opsps << "," << hitRate << "\n";
        }

        // РЕКУРСИВНЫЕ
        {
            LRUCacheRec c(cap);
            auto t = runScenario(c, workload);
            const auto& cnt = c.counters();
            double hitRate = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
            double avg = (double)t / (workload.size() + cap/2);
            double opsps = (double)(workload.size() + cap/2) / (t / 1e9);
            scsv << cap << ",LRU,rec," << t << "," << avg << "," << opsps << "," << hitRate << "\n";
        }
        {
            LFUCacheRec c(cap);
            auto t = runScenario(c, workload);
            const auto& cnt = c.counters();
            double hitRate = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
            double avg = (double)t / (workload.size() + cap/2);
            double opsps = (double)(workload.size() + cap/2) / (t / 1e9);
            scsv << cap << ",LFU,rec," << t << "," << avg << "," << opsps << "," << hitRate << "\n";
        }
    }
    scsv.close();

    std::cout << "\nCSV-файлы сохранены: results.csv и scalability.csv\n";
    std::cout << "Их можно использовать для построения графиков Python-скриптом plot_metrics.py\n";
    return 0;
}
