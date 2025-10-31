#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <memory>
#include <iomanip>
#include <fstream>
#include <string>
#include <functional>
#include <map>
#include <algorithm>

#include "CacheBase.h"
#include "LRU.h"
#include "LFU.h"
#include "Metrics.h"

using Clock = std::chrono::high_resolution_clock;
using Ns = std::chrono::nanoseconds;

struct Workload {
    std::vector<int> ops;
    int universe = 0;
    int hot_limit = 0;
};

Workload makeWorkload(int total_ops, int universe, double locality = 0.7) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> uni(0, universe - 1);
    int hot_universe = std::max(1, universe/10);
    std::uniform_int_distribution<int> hot(0, hot_universe - 1);

    Workload wl;
    wl.ops.reserve(total_ops);
    wl.universe = universe;
    wl.hot_limit = hot_universe;

    for (int i = 0; i < total_ops; ++i) {
        double p = rng() / (double)rng.max();
        if (p < locality) wl.ops.push_back(hot(rng));
        else wl.ops.push_back(uni(rng));
    }
    return wl;
}

struct RunContext {
    WarmupSeries warm;
    long long useful_evict = 0;
    long long harmful_evict = 0;
};

struct HotOracle {
    int hot_limit = 0;
    bool isHot(int key) const { return key >= 0 && key < hot_limit; }
};

long long runScenario(ICache& cache, const Workload& wl, RunContext& ctx, int window=1000) {
    HotOracle oracle{wl.hot_limit};
    g_on_evict_key = [&](int key){
        if (oracle.isHot(key)) ctx.harmful_evict++; else ctx.useful_evict++;
    };

    auto t0 = Clock::now();
    // прогрев: заполним половину ёмкости
    for (int k = 0; k < (int)cache.capacity()/2; ++k) cache.put(k, k*10);

    int ops_done = 0;
    long long last_hits = 0, last_misses = 0;
    for (int x : wl.ops) {
        if (x % 10 < 7) (void)cache.get(x);
        else cache.put(x, x*10);
        ops_done++;
        if (window > 0 && (ops_done % window == 0)) {
            const auto& c = cache.counters();
            long long dh = c.hits - last_hits;
            long long dm = c.misses - last_misses;
            double hr = (dh+dm) ? (double)dh/(dh+dm)*100.0 : 0.0;
            ctx.warm.hit_rates_over_time.push_back(hr);
            last_hits = c.hits; last_misses = c.misses;
        }
    }
    auto t1 = Clock::now();
    g_on_evict_key = nullptr;
    return std::chrono::duration_cast<Ns>(t1 - t0).count();
}

CacheMetricsRow collectRow(const char* algo, const char* impl, const ICache& c, long long elapsed_ns,
                           long long useful_evict, long long harmful_evict,
                           size_t theor_mem, size_t actual_mem, size_t overhead_mem,
                           int total_ops) {
    CacheMetricsRow row;
    row.algo = algo; row.impl = impl;
    row.capacity = (int)c.capacity();
    row.elapsed_ns = elapsed_ns;
    const auto& cnt = c.counters();
    row.gets = cnt.gets; row.puts = cnt.puts; row.evictions = cnt.evictions;
    row.hit_rate = (cnt.hits + cnt.misses) ? (double)cnt.hits / (cnt.hits + cnt.misses) * 100.0 : 0.0;
    row.miss_rate = 100.0 - row.hit_rate;
    row.avg_time_ns = total_ops ? (double)elapsed_ns / total_ops : 0.0;
    row.ops_per_sec = elapsed_ns ? (double)total_ops / (elapsed_ns / 1e9) : 0.0;
    row.useful_evictions = useful_evict;
    row.harmful_evictions = harmful_evict;
    if (row.evictions > 0) row.eviction_efficiency = (double)useful_evict / row.evictions * 100.0;
    row.theoretical_memory = theor_mem;
    row.actual_memory = actual_mem;
    row.overhead_memory = overhead_mem;
    row.memory_efficiency = theor_mem ? (double)actual_mem / theor_mem * 100.0 : 0.0;
    row.overhead_pct = actual_mem ? (double)overhead_mem / actual_mem * 100.0 : 0.0;
    return row;
}

int main() {
    const int capacity = 128;
    const int total_ops = 20000;
    const int universe = 2000;
    Workload wl = makeWorkload(total_ops, universe, 0.75);

    std::ofstream csv("results_extended.csv");
    csv << "algo,impl,capacity,elapsed_ns,gets,puts,evictions,hit_rate,miss_rate,avg_ns,ops_per_sec,"
           "useful_evictions,harmful_evictions,eviction_efficiency,"
           "theoretical_memory,actual_memory,overhead_memory,memory_efficiency,overhead_pct\n";

    LRUCacheIter lru_it(capacity);
    RunContext ctx1;
    long long t1 = runScenario(lru_it, wl, ctx1);
    size_t th=0, ac=0, ov=0; lru_it.estimateMemory(th,ac,ov);
    auto r1 = collectRow("LRU","iter", lru_it, t1, ctx1.useful_evict, ctx1.harmful_evict, th, ac, ov, total_ops + capacity/2);

    LRUCacheRec lru_rc(capacity);
    RunContext ctx2;
    long long t2 = runScenario(lru_rc, wl, ctx2);
    lru_rc.estimateMemory(th,ac,ov);
    auto r2 = collectRow("LRU","rec",  lru_rc, t2, ctx2.useful_evict, ctx2.harmful_evict, th, ac, ov, total_ops + capacity/2);

    LFUCacheIter lfu_it(capacity);
    RunContext ctx3;
    long long t3 = runScenario(lfu_it, wl, ctx3);
    lfu_it.estimateMemory(th,ac,ov);
    auto r3 = collectRow("LFU","iter", lfu_it, t3, ctx3.useful_evict, ctx3.harmful_evict, th, ac, ov, total_ops + capacity/2);

    LFUCacheRec lfu_rc(capacity);
    RunContext ctx4;
    long long t4 = runScenario(lfu_rc, wl, ctx4);
    lfu_rc.estimateMemory(th,ac,ov);
    auto r4 = collectRow("LFU","rec",  lfu_rc, t4, ctx4.useful_evict, ctx4.harmful_evict, th, ac, ov, total_ops + capacity/2);

    auto dumpRow = [&](const CacheMetricsRow& r){
        csv << r.algo << "," << r.impl << "," << r.capacity << "," << r.elapsed_ns << ","
            << r.gets << "," << r.puts << "," << r.evictions << ","
            << r.hit_rate << "," << r.miss_rate << "," << r.avg_time_ns << "," << r.ops_per_sec << ","
            << r.useful_evictions << "," << r.harmful_evictions << "," << r.eviction_efficiency << ","
            << r.theoretical_memory << "," << r.actual_memory << "," << r.overhead_memory << ","
            << r.memory_efficiency << "," << r.overhead_pct << "\n";
    };
    dumpRow(r1); dumpRow(r2); dumpRow(r3); dumpRow(r4);
    csv.close();

    std::vector<int> sizes = {16,32,64,128,256,512,1024};
    std::ofstream scsv("scalability_extended.csv");
    scsv << "size,algo,impl,elapsed_ns,avg_ns,ops_per_sec,hit_rate,useful_evictions,harmful_evictions,eviction_efficiency\n";

    for (int cap : sizes) {
        Workload wl2 = makeWorkload(15000, 4000, 0.75);
        {
            LRUCacheIter c(cap);
            RunContext rc;
            auto t = runScenario(c, wl2, rc);
            const auto& cnt = c.counters();
            double hr = (cnt.hits+cnt.misses)?(double)cnt.hits/(cnt.hits+cnt.misses)*100.0:0.0;
            double avg = (double)t / (wl2.ops.size() + cap/2);
            double opsps = (double)(wl2.ops.size()+cap/2) / (t/1e9);
            double eff = (cnt.evictions>0)? (double)rc.useful_evict / cnt.evictions * 100.0 : 0.0;
            scsv << cap << ",LRU,iter," << t << "," << avg << "," << opsps << "," << hr << ","
                 << rc.useful_evict << "," << rc.harmful_evict << "," << eff << "\n";
        }
        {
            LFUCacheIter c(cap);
            RunContext rc;
            auto t = runScenario(c, wl2, rc);
            const auto& cnt = c.counters();
            double hr = (cnt.hits+cnt.misses)?(double)cnt.hits/(cnt.hits+cnt.misses)*100.0:0.0;
            double avg = (double)t / (wl2.ops.size() + cap/2);
            double opsps = (double)(wl2.ops.size()+cap/2) / (t/1e9);
            double eff = (cnt.evictions>0)? (double)rc.useful_evict / cnt.evictions * 100.0 : 0.0;
            scsv << cap << ",LFU,iter," << t << "," << avg << "," << opsps << "," << hr << ","
                 << rc.useful_evict << "," << rc.harmful_evict << "," << eff << "\n";
        }
        {
            LRUCacheRec c(cap);
            RunContext rc;
            auto t = runScenario(c, wl2, rc);
            const auto& cnt = c.counters();
            double hr = (cnt.hits+cnt.misses)?(double)cnt.hits/(cnt.hits+cnt.misses)*100.0:0.0;
            double avg = (double)t / (wl2.ops.size() + cap/2);
            double opsps = (double)(wl2.ops.size()+cap/2) / (t/1e9);
            double eff = (cnt.evictions>0)? (double)rc.useful_evict / cnt.evictions * 100.0 : 0.0;
            scsv << cap << ",LRU,rec," << t << "," << avg << "," << opsps << "," << hr << ","
                 << rc.useful_evict << "," << rc.harmful_evict << "," << eff << "\n";
        }
        {
            LFUCacheRec c(cap);
            RunContext rc;
            auto t = runScenario(c, wl2, rc);
            const auto& cnt = c.counters();
            double hr = (cnt.hits+cnt.misses)?(double)cnt.hits/(cnt.hits+cnt.misses)*100.0:0.0;
            double avg = (double)t / (wl2.ops.size() + cap/2);
            double opsps = (double)(wl2.ops.size()+cap/2) / (t/1e9);
            double eff = (cnt.evictions>0)? (double)rc.useful_evict / cnt.evictions * 100.0 : 0.0;
            scsv << cap << ",LFU,rec," << t << "," << avg << "," << opsps << "," << hr << ","
                 << rc.useful_evict << "," << rc.harmful_evict << "," << eff << "\n";
        }
    }
    scsv.close();

    std::ofstream stabcsv("stability.csv");
    stabcsv << "algo,impl,trial,ops_per_sec\n";
    const int trials = 5;
    auto runTrials = [&](const char* algo, const char* impl, auto cacheFactory){
        StabilityMetrics sm;
        for (int i=0;i<trials;++i){
            auto cache = cacheFactory();
            RunContext rc; (void)rc;
            long long t = runScenario(*cache, wl, rc);
            double opsps = (double)(wl.ops.size() + cache->capacity()/2) / (t/1e9);
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

    std::ofstream roicsv("roi.csv");
    roicsv << "algo,impl,roi,perf_score,resource,impl_cost,maint_cost\n";
    auto emitROI = [&](const CacheMetricsRow& r){
        double resource = r.actual_memory/1024.0 + r.elapsed_ns/1e8;
        double impl_cost = (r.impl=="iter") ? 2.0 : 1.5;
        double maint_cost = (r.impl=="iter") ? 1.5 : 1.0;
        double perf_score = r.ops_per_sec * (r.hit_rate/100.0);
        double roi = CostEffectiveness::ROI(perf_score, resource, impl_cost, maint_cost);
        roicsv << r.algo << "," << r.impl << "," << roi << "," << perf_score << ","
               << resource << "," << impl_cost << "," << maint_cost << "\n";
    };
    emitROI(r1); emitROI(r2); emitROI(r3); emitROI(r4);
    roicsv.close();

    std::cout << "\nCSV-файлы сохранены:\n"
              << "  - results_extended.csv\n"
              << "  - scalability_extended.csv\n"
              << "  - stability.csv\n"
              << "  - efficiency_score.csv\n"
              << "  - roi.csv\n";
    return 0;
}
