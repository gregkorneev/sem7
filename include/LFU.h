#pragma once
#include "CacheBase.h"
#include <unordered_map>
#include <list>
#include <optional>

/**
 * ===================== ИТЕРАЦИОННЫЙ LFU =====================
 * Эффективная реализация через "корзины частот":
 *  - pos_: key -> итератор на узел в списке нужной частоты
 *  - buckets_: freq -> список узлов с этой частотой
 *  - minFreq_: минимальная частота в кэше (для O(1) вытеснения)
 */
class LFUCacheIter : public ICache {
public:
    explicit LFUCacheIter(size_t cap);
    void put(int key, int value) override;
    std::optional<int> get(int key) override;
    size_t size() const override { return sz_; }
    size_t capacity() const override { return cap_; }
    const OpCounters& counters() const override { return cnt_; }

private:
    struct Node { int key, val, freq; };
    size_t cap_, sz_ = 0, minFreq_ = 0;
    std::unordered_map<int, std::list<Node>::iterator> pos_;
    std::unordered_map<int, std::list<Node>> buckets_;
    OpCounters cnt_;

    void touch(std::unordered_map<int, std::list<Node>::iterator>::iterator it);
    void evictOne();
};

/**
 * ===================== РЕКУРСИВНЫЙ LFU =====================
 * Для демонстрации рекурсии реализуем LFU на простом односвязном списке:
 *  - Поиск по ключу, инкремент частоты, поиск минимума и удаление — рекурсивно.
 *  - Асимптотика операций O(n), но код компактный и наглядный.
 */
class LFUCacheRec : public ICache {
public:
    explicit LFUCacheRec(size_t cap);
    ~LFUCacheRec();

    void put(int key, int value) override;
    std::optional<int> get(int key) override;
    size_t size() const override { return sz_; }
    size_t capacity() const override { return cap_; }
    const OpCounters& counters() const override { return cnt_; }

private:
    struct Node { int key, val, freq; Node* next; };
    Node* head_ = nullptr;
    size_t cap_, sz_ = 0;
    OpCounters cnt_;

    // рекурсивные помощники
    std::optional<int> getRec(Node* cur, int key);
    bool putUpdateRec(Node* cur, int key, int value); // обновить value и ++freq если найден
    // поиск узла с минимальной freq — вернуть prev к минимальному и сам минимум
    std::pair<Node*, Node*> findMinPrevRec(Node* prev, Node* cur, Node* bestPrev, Node* best);
    void freeList(Node* n);
};
