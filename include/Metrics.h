#pragma once
#include <vector>
#include <cmath>
#include <cstddef>
#include <string>
#include <numeric>

struct CacheMetricsRow {
    std::string algo;
    std::string impl;
    int capacity = 0;
    long long elapsed_ns = 0;
    long long gets = 0;
    long long puts = 0;
    long long evictions = 0;
    double hit_rate = 0.0;
    double miss_rate = 0.0;
    double avg_time_ns = 0.0;
    double ops_per_sec = 0.0;
    long long useful_evictions = 0;
    long long harmful_evictions = 0;
    double eviction_efficiency = 0.0;
    size_t theoretical_memory = 0;
    size_t actual_memory = 0;
    size_t overhead_memory = 0;
    double memory_efficiency = 0.0;
    double overhead_pct = 0.0;
};

struct WarmupSeries {
    std::vector<double> hit_rates_over_time;
    long long warmup_operations = 0;
    int detectWarmupWindow(double eps = 1.0) const {
        for (size_t i = 1; i < hit_rates_over_time.size(); ++i) {
            if (std::abs(hit_rates_over_time[i] - hit_rates_over_time[i-1]) < eps) return (int)i;
        }
        return (int)hit_rates_over_time.size();
    }
};

struct StabilityMetrics {
    std::vector<double> samples;
    double avg = 0.0;
    double stddev = 0.0;
    double cov = 0.0;
    void compute() {
        if (samples.empty()) { avg = stddev = cov = 0.0; return; }
        avg = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
        double s2 = 0.0;
        for (double x : samples) s2 += (x - avg)*(x - avg);
        stddev = std::sqrt(s2 / samples.size());
        cov = (avg != 0.0) ? (stddev / avg * 100.0) : 0.0;
    }
};

struct EfficiencyScore {
    double hit_rate_weight = 0.4;
    double speed_weight    = 0.3;
    double memory_weight   = 0.2;
    double stability_weight= 0.1;
    double calculate(double hit_rate, double avg_ns, double memory_eff, double stability_score) const {
        double hit_score = hit_rate / 100.0;
        double speed_score = 1.0 / (1.0 + (avg_ns / 1000.0));
        double mem_score = memory_eff / 100.0;
        double stab_score = stability_score / 100.0;
        return hit_score*hit_rate_weight + speed_score*speed_weight + mem_score*memory_weight + stab_score*stability_weight;
    }
};

inline double stabilityScoreFromCov(double cov) {
    return 100.0 / (1.0 + cov);
}

struct CostEffectiveness {
    static double ROI(double performance_score, double resource_usage, double impl_cost, double maint_cost) {
        return performance_score / (resource_usage + impl_cost + maint_cost + 1e-9);
    }
};
