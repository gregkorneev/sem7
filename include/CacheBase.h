#pragma once
/**
 * Common cache interface + operation counters used by metrics.
 *
 * Мы делаем общий интерфейс ICache, чтобы одинаково работать с четырьмя реализациями:
 * - LRUCacheIter  (итеративный LRU)
 * - LRUCacheRec   (рекурсивный LRU)
 * - LFUCacheIter  (итеративный LFU)
 * - LFUCacheRec   (рекурсивный LFU)
 *
 * Все кэши поддерживают put(key, value) и get(key) -> optional<int>.
 * Дополнительно возвращаем счётчики операций (hits/misses/puts/gets/evictions),
 * чтобы легко считать метрики в main.cpp.
 */
#include <optional>
#include <cstddef>

struct OpCounters {
    long long hits = 0;
    long long misses = 0;
    long long puts = 0;
    long long gets = 0;
    long long evictions = 0;
};

class ICache {
public:
    virtual ~ICache() = default;
    virtual void put(int key, int value) = 0;
    virtual std::optional<int> get(int key) = 0;
    virtual size_t size() const = 0;
    virtual size_t capacity() const = 0;
    virtual const OpCounters& counters() const = 0;
};
