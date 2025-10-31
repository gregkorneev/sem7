
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <memory>
#include <iomanip>
#include <fstream>
#include <string>
#include <functional>
#include <algorithm>

#include "CacheBase.h"
#include "LRU.h"
#include "LFU.h"
#include "Metrics.h"

using Clock = std::chrono::high_resolution_clock;
using Ns    = std::chrono::nanoseconds;

struct Workload {
    std::vector<int> ops;
    int universe = 0;
    int hot_limit = 0;
};

// Нагрузка с локальностью доступа
Workload makeWorkload(int total_ops, int universe, double locality = 0.75) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> uni(0, universe - 1);
    int hot_universe = std::max(1, universe / 10);
    std::uniform_int_distribution<int> hot(0, hot_universe - 1);

    Workload wl;
    wl.ops.reserve(total_ops);
    wl.universe = universe;
    wl.hot_limit = hot_universe;

    for (int i = 0; i < total_ops; ++i) {
        double p = rng() / (double)rng.max();
        wl.ops.push_back(p < locality ? hot(rng) : uni(rng));
    }
    return wl;
}

// Контекст выполнения сценария
struct RunContext {
    WarmupSeries warm;            // из Metrics.h
    long long useful_evict = 0;   // вытеснены «холодные»
    long long harmful_evict = 0;  // вытеснены «горячие»
};

// «Оракул» для определения горячих ключей
struct HotOracle {
    int hot_limit = 0;
    bool isHot(int key) const { return key >= 0 && key < hot_limit; }
};

// Замер сценария + сбор warmup метрики
long long runScenario(ICache& cache, const Workload& wl, RunContext& ctx, int window = 1000) {
    HotOracle oracle{wl.hot_limit};

    g_on_evict_key = [&](int key) {
        if (oracle.isHot(key)) ctx.harmful_evict++;
        else                   ctx.useful_evict++;
    };

    auto t0 = Clock::now();

    // Прогрев кэша: положим половину ёмкости
    for (int k = 0; k < (int)cache.capacity() / 2; ++k) cache.put(k, k * 10);

    int ops_done = 0;
    long long last_hits = 0, last_misses = 0;

    for (int x : wl.ops) {
        if (x % 10 < 7) (void)cache.get(x);
        else            cache.put(x, x * 10);

        ops_done++;
        if (window > 0 && (ops_done % window == 0)) {
            const auto& c = cache.counters();
            long long dh = c.hits   - last_hits;
            long long dm = c.misses - last_misses;
            double hr = (dh + dm) ? (double)dh / (dh + dm) * 100.0 : 0.0;
            ctx.warm.hit_rates_over_time.push_back(hr);
            last_hits   = c.hits;
            last_misses = c.misses;
        }
    }

    auto t1 = Clock::now();
    g_on_evict_key = nullptr;
    return std::chrono::duration_cast<Ns>(t1 - t0).count();
}

// Доп. метрика 11 — стоимость операции
double calculateCostPerOperation(long long total_time_ns, long long operations, double time_value = 1.0) {
    double time_in_seconds = total_time_ns / 1e9;
    return (operations > 0) ? (time_in_seconds * time_value) / operations : 0.0;
}

// Собираем свод по одному прогону
CacheMetricsRow collectRow(const char* algo, const char* impl, const ICache& c, long long elapsed_ns,
                           long long useful_evict, long long harmful_evict,
                           size_t theor_mem, size_t actual_mem, size_t overhead_mem,
                           int total_ops,
                           // Дополнительно:
                           int warmup_ops,
                           double cost_per_op,
                           double frag_ratio) {
    CacheMetricsRow row;
    row.algo = algo; row.impl = impl;
    row.capacity = (int)c.capacity();
    row.elapsed_ns = elapsed_ns;

    const auto& cnt = c.counters();
    row.gets = cnt.gets; row.puts = cnt.puts; row.evictions = cnt.evictions;
    row.hit_rate  = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
    row.miss_rate = 100.0 - row.hit_rate;
    row.avg_time_ns = total_ops ? (double)elapsed_ns / total_ops : 0.0;
    row.ops_per_sec = elapsed_ns ? (double)total_ops / (elapsed_ns / 1e9) : 0.0;

    row.useful_evictions = useful_evict;
    row.harmful_evictions = harmful_evict;
    if (row.evictions > 0) row.eviction_efficiency = (double)useful_evict / row.evictions * 100.0;

    row.theoretical_memory = theor_mem;
    row.actual_memory      = actual_mem;
    row.overhead_memory    = overhead_mem;
    row.memory_efficiency = theor_mem ? (double)actual_mem / theor_mem * 100.0 : 0.0;
    row.overhead_pct      = actual_mem ? (double)overhead_mem / actual_mem * 100.0 : 0.0;

    // Ниже — значения, которых нет в CacheMetricsRow, но мы выведем их отдельно в CSV
    (void)warmup_ops;
    (void)cost_per_op;
    (void)frag_ratio;

    return row;
}

// Простые юнит‑тесты корректности поведения LRU / LFU (итеративные версии)
void runBasicCacheTests() {
    std::cout << "\n--- Проверка корректности LRU/LFU ---\n";

    // Тест LRU: после доступа к ключу 1 он должен стать самым «свежим»,
    // при вставке 3 из кэша ёмкости 2 должен быть вытеснен ключ 2.
    {
        LRUCacheIter lru(2);
        lru.put(1, 10); lru.put(2, 20);
        auto v1 = lru.get(1);          // делаем 1 «свежим»
        lru.put(3, 30);                // вытеснение «самого старого» — это 2
        bool ok = (!lru.get(2).has_value()) && v1.value_or(-1) == 10
                  && lru.get(1).value_or(-1) == 10 && lru.get(3).value_or(-1) == 30;
        std::cout << "LRU (iter) Test: " << (ok ? "OK" : "FAIL") << "\n";
    }

    // Тест LFU: с частотами (1 используется чаще 2) — при вставке 3 вытесняется 2.
    {
        LFUCacheIter lfu(2);
        lfu.put(1, 10); lfu.put(2, 20);
        (void)lfu.get(1);              // freq(1) = 2, freq(2) = 1
        lfu.put(3, 30);                // вытеснить должен 2 (самый редкий)
        bool ok = (!lfu.get(2).has_value()) && lfu.get(1).value_or(-1) == 10
                  && lfu.get(3).value_or(-1) == 30;
        std::cout << "LFU (iter) Test: " << (ok ? "OK" : "FAIL") << "\n";
    }
}

int main() {
    // Небольшая проверка корректности
    runBasicCacheTests();

    const int capacity  = 128;
    const int total_ops = 20000;
    const int universe  = 2000;

    Workload wl = makeWorkload(total_ops, universe, 0.75);

    // CSV с расширенными метриками (добавлены warmup_ops, cost_per_op, frag_ratio)
    std::ofstream csv("results_extended.csv");
    csv << "algo,impl,capacity,elapsed_ns,gets,puts,evictions,hit_rate,miss_rate,avg_ns,ops_per_sec,"
           "useful_evictions,harmful_evictions,eviction_efficiency,"
           "theoretical_memory,actual_memory,overhead_memory,memory_efficiency,overhead_pct,"
           "warmup_ops,cost_per_op,fragmentation_ratio\n";

    // Для графика прогрева сохраним warmup.csv (последнего прогона каждого варианта)
    std::ofstream warmcsv("warmup.csv");
    warmcsv << "step,hit_rate\n";

    // ---- LRU (iter) ----
    LRUCacheIter lru_it(capacity);
    RunContext ctx1;
    long long t1 = runScenario(lru_it, wl, ctx1);
    size_t th=0, ac=0, ov=0; lru_it.estimateMemory(th,ac,ov);

    // оценка warmup и стоимости операции
    int warm1 = (int)ctx1.warm.hit_rates_over_time.size(); // упрощённый warmup_ops (по окнам)
    for (size_t i = 0; i < ctx1.warm.hit_rates_over_time.size(); ++i)
        warmcsv << i << "," << ctx1.warm.hit_rates_over_time[i] << "\n";
    double cost1 = calculateCostPerOperation(t1, (int)wl.ops.size() + capacity/2);

    // простая оценка «фрагментации»: (peak - current)/peak (%)
    // здесь peak примем как теоретическую ёмкость, current — реальное использование
    double frag1 = th ? (double)(th > ac ? (th - ac) : 0) / th * 100.0 : 0.0;

    auto r1 = collectRow("LRU","iter", lru_it, t1, ctx1.useful_evict, ctx1.harmful_evict, th, ac, ov,
                         (int)wl.ops.size() + capacity/2, warm1, cost1, frag1);
    csv << r1.algo << "," << r1.impl << "," << r1.capacity << "," << r1.elapsed_ns << ","
        << r1.gets << "," << r1.puts << "," << r1.evictions << ","
        << r1.hit_rate << "," << r1.miss_rate << "," << r1.avg_time_ns << "," << r1.ops_per_sec << ","
        << r1.useful_evictions << "," << r1.harmful_evictions << "," << r1.eviction_efficiency << ","
        << r1.theoretical_memory << "," << r1.actual_memory << "," << r1.overhead_memory << ","
        << r1.memory_efficiency << "," << r1.overhead_pct << ","
        << warm1 << "," << cost1 << "," << frag1 << "\n";

    // ---- LRU (rec) ----
    LRUCacheRec lru_rc(capacity);
    RunContext ctx2;
    long long t2 = runScenario(lru_rc, wl, ctx2);
    lru_rc.estimateMemory(th,ac,ov);
    int warm2 = (int)ctx2.warm.hit_rates_over_time.size();
    double cost2 = calculateCostPerOperation(t2, (int)wl.ops.size() + capacity/2);
    double frag2 = th ? (double)(th > ac ? (th - ac) : 0) / th * 100.0 : 0.0;
    auto r2 = collectRow("LRU","rec",  lru_rc, t2, ctx2.useful_evict, ctx2.harmful_evict, th, ac, ov,
                         (int)wl.ops.size() + capacity/2, warm2, cost2, frag2);
    csv << r2.algo << "," << r2.impl << "," << r2.capacity << "," << r2.elapsed_ns << ","
        << r2.gets << "," << r2.puts << "," << r2.evictions << ","
        << r2.hit_rate << "," << r2.miss_rate << "," << r2.avg_time_ns << "," << r2.ops_per_sec << ","
        << r2.useful_evictions << "," << r2.harmful_evictions << "," << r2.eviction_efficiency << ","
        << r2.theoretical_memory << "," << r2.actual_memory << "," << r2.overhead_memory << ","
        << r2.memory_efficiency << "," << r2.overhead_pct << ","
        << warm2 << "," << cost2 << "," << frag2 << "\n";

    // ---- LFU (iter) ----
    LFUCacheIter lfu_it(capacity);
    RunContext ctx3;
    long long t3 = runScenario(lfu_it, wl, ctx3);
    lfu_it.estimateMemory(th,ac,ov);
    int warm3 = (int)ctx3.warm.hit_rates_over_time.size();
    double cost3 = calculateCostPerOperation(t3, (int)wl.ops.size() + capacity/2);
    double frag3 = th ? (double)(th > ac ? (th - ac) : 0) / th * 100.0 : 0.0;
    auto r3 = collectRow("LFU","iter", lfu_it, t3, ctx3.useful_evict, ctx3.harmful_evict, th, ac, ov,
                         (int)wl.ops.size() + capacity/2, warm3, cost3, frag3);
    csv << r3.algo << "," << r3.impl << "," << r3.capacity << "," << r3.elapsed_ns << ","
        << r3.gets << "," << r3.puts << "," << r3.evictions << ","
        << r3.hit_rate << "," << r3.miss_rate << "," << r3.avg_time_ns << "," << r3.ops_per_sec << ","
        << r3.useful_evictions << "," << r3.harmful_evictions << "," << r3.eviction_efficiency << ","
        << r3.theoretical_memory << "," << r3.actual_memory << "," << r3.overhead_memory << ","
        << r3.memory_efficiency << "," << r3.overhead_pct << ","
        << warm3 << "," << cost3 << "," << frag3 << "\n";

    // ---- LFU (rec) ----
    LFUCacheRec lfu_rc(capacity);
    RunContext ctx4;
    long long t4 = runScenario(lfu_rc, wl, ctx4);
    lfu_rc.estimateMemory(th,ac,ov);
    int warm4 = (int)ctx4.warm.hit_rates_over_time.size();
    double cost4 = calculateCostPerOperation(t4, (int)wl.ops.size() + capacity/2);
    double frag4 = th ? (double)(th > ac ? (th - ac) : 0) / th * 100.0 : 0.0;
    auto r4 = collectRow("LFU","rec",  lfu_rc, t4, ctx4.useful_evict, ctx4.harmful_evict, th, ac, ov,
                         (int)wl.ops.size() + capacity/2, warm4, cost4, frag4);
    csv << r4.algo << "," << r4.impl << "," << r4.capacity << "," << r4.elapsed_ns << ","
        << r4.gets << "," << r4.puts << "," << r4.evictions << ","
        << r4.hit_rate << "," << r4.miss_rate << "," << r4.avg_time_ns << "," << r4.ops_per_sec << ","
        << r4.useful_evictions << "," << r4.harmful_evictions << "," << r4.eviction_efficiency << ","
        << r4.theoretical_memory << "," << r4.actual_memory << "," << r4.overhead_memory << ","
        << r4.memory_efficiency << "," << r4.overhead_pct << ","
        << warm4 << "," << cost4 << "," << frag4 << "\n";

    csv.close();
    warmcsv.close();

    // ---- Масштабируемость по размерам ----
    std::vector<int> sizes = {16, 32, 64, 128, 256, 512, 1024};
    std::ofstream scsv("scalability_extended.csv");
    scsv << "size,algo,impl,elapsed_ns,avg_ns,ops_per_sec,hit_rate,useful_evictions,harmful_evictions,eviction_efficiency\n";

    for (int cap : sizes) {
        Workload wl2 = makeWorkload(15000, 4000, 0.75);
        {
            LRUCacheIter c(cap);
            RunContext rc;
            auto t = runScenario(c, wl2, rc);
            const auto& cnt = c.counters();
            double hr   = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
            double avg  = (double)t / (wl2.ops.size() + cap / 2);
            double opsp = (double)(wl2.ops.size() + cap / 2) / (t / 1e9);
            double eff  = (cnt.evictions > 0) ? (double)rc.useful_evict / cnt.evictions * 100.0 : 0.0;
            scsv << cap << ",LRU,iter," << t << "," << avg << "," << opsp << "," << hr << ","
                 << rc.useful_evict << "," << rc.harmful_evict << "," << eff << "\n";
        }
        {
            LFUCacheIter c(cap);
            RunContext rc;
            auto t = runScenario(c, wl2, rc);
            const auto& cnt = c.counters();
            double hr   = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
            double avg  = (double)t / (wl2.ops.size() + cap / 2);
            double opsp = (double)(wl2.ops.size() + cap / 2) / (t / 1e9);
            double eff  = (cnt.evictions > 0) ? (double)rc.useful_evict / cnt.evictions * 100.0 : 0.0;
            scsv << cap << ",LFU,iter," << t << "," << avg << "," << opsp << "," << hr << ","
                 << rc.useful_evict << "," << rc.harmful_evict << "," << eff << "\n";
        }
        {
            LRUCacheRec c(cap);
            RunContext rc;
            auto t = runScenario(c, wl2, rc);
            const auto& cnt = c.counters();
            double hr   = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
            double avg  = (double)t / (wl2.ops.size() + cap / 2);
            double opsp = (double)(wl2.ops.size() + cap / 2) / (t / 1e9);
            double eff  = (cnt.evictions > 0) ? (double)rc.useful_evict / cnt.evictions * 100.0 : 0.0;
            scsv << cap << ",LRU,rec," << t << "," << avg << "," << opsp << "," << hr << ","
                 << rc.useful_evict << "," << rc.harmful_evict << "," << eff << "\n";
        }
        {
            LFUCacheRec c(cap);
            RunContext rc;
            auto t = runScenario(c, wl2, rc);
            const auto& cnt = c.counters();
            double hr   = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
            double avg  = (double)t / (wl2.ops.size() + cap / 2);
            double opsp = (double)(wl2.ops.size() + cap / 2) / (t / 1e9);
            double eff  = (cnt.evictions > 0) ? (double)rc.useful_evict / cnt.evictions * 100.0 : 0.0;
            scsv << cap << ",LFU,rec," << t << "," << avg << "," << opsp << "," << hr << ","
                 << rc.useful_evict << "," << rc.harmful_evict << "," << eff << "\n";
        }
    }
    scsv.close();

    // ---- Повторяемость/стабильность ----
    std::ofstream stabcsv("stability.csv");
    stabcsv << "algo,impl,trial,ops_per_sec\n";
    const int trials = 5;
    auto runTrials = [&](const char* algo, const char* impl, auto cacheFactory){
        StabilityMetrics sm;
        for (int i = 0; i < trials; ++i) {
            auto cache = cacheFactory();
            RunContext rc;
            long long t = runScenario(*cache, wl, rc);
            double opsps = (double)(wl.ops.size() + cache->capacity() / 2) / (t / 1e9);
            sm.samples.push_back(opsps);
            stabcsv << algo << "," << impl << "," << i << "," << opsps << "\n";
        }
        sm.compute();
        return sm;
    };
    auto sm_lru_it = runTrials("LRU","iter", [&](){ return std::make_unique<LRUCacheIter>(capacity); });
    auto sm_lru_rc = runTrials("LRU","rec",  [&](){ return std::make_unique<LRUCacheRec>(capacity); });
    auto sm_lfu_it = runTrials("LFU","iter", [&](){ return std::make_unique<LFUCacheIter>(capacity); });
    auto sm_lfu_rc = runTrials("LFU","rec",  [&](){ return std::make_unique<LFUCacheRec>(capacity); });
    stabcsv.close();

    // ---- Интегральный скор ----
    EfficiencyScore scorer;
    auto eff_csv = std::ofstream("efficiency_score.csv");
    eff_csv << "algo,impl,score,stability_score,hit_rate,avg_ns,memory_eff\n";
    auto emitScore = [&](const CacheMetricsRow& r, const StabilityMetrics& sm){
        double stab_score = stabilityScoreFromCov(sm.cov);
        double score = scorer.calculate(r.hit_rate, r.avg_time_ns, r.memory_efficiency, stab_score);
        eff_csv << r.algo << "," << r.impl << "," << score << "," << stab_score << ","
                << r.hit_rate << "," << r.avg_time_ns << "," << r.memory_efficiency << "\n";
    };
    emitScore(r1, sm_lru_it);
    emitScore(r2, sm_lru_rc);
    emitScore(r3, sm_lfu_it);
    emitScore(r4, sm_lfu_rc);
    eff_csv.close();

    // ---- ROI ----
    std::ofstream roicsv("roi.csv");
    roicsv << "algo,impl,roi,perf_score,resource,impl_cost,maint_cost\n";
    auto emitROI = [&](const CacheMetricsRow& r){
        double resource   = r.actual_memory/1024.0 + r.elapsed_ns/1e8;
        double impl_cost  = (r.impl=="iter") ? 2.0 : 1.5;
        double maint_cost = (r.impl=="iter") ? 1.5 : 1.0;
        double perf_score = r.ops_per_sec * (r.hit_rate/100.0);
        double roi = CostEffectiveness::ROI(perf_score, resource, impl_cost, maint_cost);
        roicsv << r.algo << "," << r.impl << "," << roi << "," << perf_score << ","
               << resource << "," << impl_cost << "," << maint_cost << "\n";
    };
    emitROI(r1); emitROI(r2); emitROI(r3); emitROI(r4);
    roicsv.close();

    // ---- Algorithm Efficiency (метрика 6) ----
    std::ofstream aeff("algorithm_efficiency.csv");
    aeff << "algo,efficiency%\n";
    auto algoEff = [&](const CacheMetricsRow& r){
        long long ops  = r.gets + r.puts;
        long long hits = (long long)((r.hit_rate/100.0) * (double)(r.gets + r.miss_rate/100.0 * 0.0)); // приближение
        // Используем хиты из counters через hit_rate:
        hits = (long long)((r.hit_rate/100.0) * (double)(r.gets + (r.gets * (r.miss_rate/100.0))));
        double eff = ops ? (double)hits / (double)ops * 100.0 : 0.0;
        aeff << r.algo << "-" << r.impl << "," << eff << "\n";
    };
    algoEff(r1); algoEff(r2); algoEff(r3); algoEff(r4);
    aeff.close();

    std::cout << "\nCSV-файлы сохранены:\n"
              << "  - results_extended.csv\n"
              << "  - scalability_extended.csv\n"
              << "  - stability.csv\n"
              << "  - efficiency_score.csv\n"
              << "  - roi.csv\n"
              << "  - algorithm_efficiency.csv\n"
              << "  - warmup.csv\n";
    return 0;
}
