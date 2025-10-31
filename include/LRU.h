#pragma once
#include "CacheBase.h"
#include <list>
#include <unordered_map>
#include <optional>

class LRUCacheIter : public ICache {
public:
    explicit LRUCacheIter(size_t cap);
    void put(int key, int value) override;
    std::optional<int> get(int key) override;
    size_t size() const override { return order_.size(); }
    size_t capacity() const override { return cap_; }
    const OpCounters& counters() const override { return cnt_; }
    void estimateMemory(size_t& theoretical, size_t& actual, size_t& overhead) const;
private:
    using Node = std::pair<int,int>;
    size_t cap_;
    std::list<Node> order_;
    std::unordered_map<int, std::list<Node>::iterator> pos_;
    OpCounters cnt_;
    void touch(std::unordered_map<int, std::list<Node>::iterator>::iterator it);
};

class LRUCacheRec : public ICache {
public:
    explicit LRUCacheRec(size_t cap);
    ~LRUCacheRec();
    void put(int key, int value) override;
    std::optional<int> get(int key) override;
    size_t size() const override { return sz_; }
    size_t capacity() const override { return cap_; }
    const OpCounters& counters() const override { return cnt_; }
    void estimateMemory(size_t& theoretical, size_t& actual, size_t& overhead) const;
    long long total_allocations() const { return allocations_; }
    long long total_deallocations() const { return deallocations_; }
private:
    struct Node { int key, val; Node* next; };
    Node* head_ = nullptr;
    size_t cap_;
    size_t sz_ = 0;
    OpCounters cnt_;
    long long allocations_ = 0;
    long long deallocations_ = 0;
    std::optional<int> getRec(Node* prev, Node* cur, int key);
    bool putUpdateRec(Node* prev, Node* cur, int key, int value);
    Node* removeTailRec(Node* cur, bool& removed);
    void freeList(Node* n);
};
