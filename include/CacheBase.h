#pragma once
#include <optional>
#include <cstddef>
#include <functional>

struct OpCounters {
    long long hits = 0;
    long long misses = 0;
    long long puts = 0;
    long long gets = 0;
    long long evictions = 0;
};

extern std::function<void(int)> g_on_evict_key;

class ICache {
public:
    virtual ~ICache() = default;
    virtual void put(int key, int value) = 0;
    virtual std::optional<int> get(int key) = 0;
    virtual size_t size() const = 0;
    virtual size_t capacity() const = 0;
    virtual const OpCounters& counters() const = 0;
};
