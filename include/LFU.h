#pragma once
#include "CacheBase.h"
#include <unordered_map>
#include <list>
#include <optional>

class LFUCacheIter : public ICache {
public:
    explicit LFUCacheIter(size_t cap);
    void put(int key, int value) override;
    std::optional<int> get(int key) override;
    size_t size() const override { return sz_; }
    size_t capacity() const override { return cap_; }
    const OpCounters& counters() const override { return cnt_; }
    void estimateMemory(size_t& theoretical, size_t& actual, size_t& overhead) const;
private:
    struct Node { int key, val, freq; };
    size_t cap_, sz_ = 0, minFreq_ = 0;
    std::unordered_map<int, std::list<Node>::iterator> pos_;
    std::unordered_map<int, std::list<Node>> buckets_;
    OpCounters cnt_;
    void touch(std::unordered_map<int, std::list<Node>::iterator>::iterator it);
    void evictOne();
};

class LFUCacheRec : public ICache {
public:
    explicit LFUCacheRec(size_t cap);
    ~LFUCacheRec();
    void put(int key, int value) override;
    std::optional<int> get(int key) override;
    size_t size() const override { return sz_; }
    size_t capacity() const override { return cap_; }
    const OpCounters& counters() const override { return cnt_; }
    void estimateMemory(size_t& theoretical, size_t& actual, size_t& overhead) const;
    long long total_allocations() const { return allocations_; }
    long long total_deallocations() const { return deallocations_; }
private:
    struct Node { int key, val, freq; Node* next; };
    Node* head_ = nullptr;
    size_t cap_, sz_ = 0;
    OpCounters cnt_;
    long long allocations_ = 0;
    long long deallocations_ = 0;
    std::optional<int> getRec(Node* cur, int key);
    bool putUpdateRec(Node* cur, int key, int value);
    std::pair<Node*, Node*> findMinPrevRec(Node* prev, Node* cur, Node* bestPrev, Node* best);
    void freeList(Node* n);
};
