#include "LRU.h"
#include "CacheBase.h"

LRUCacheIter::LRUCacheIter(size_t cap) : cap_(cap) {}

void LRUCacheIter::touch(std::unordered_map<int, std::list<Node>::iterator>::iterator it) {
    auto nodeIt = it->second;
    order_.splice(order_.begin(), order_, nodeIt);
}

std::optional<int> LRUCacheIter::get(int key) {
    cnt_.gets++;
    auto it = pos_.find(key);
    if (it == pos_.end()) { cnt_.misses++; return std::nullopt; }
    touch(it);
    cnt_.hits++;
    return it->second->second;
}

void LRUCacheIter::put(int key, int value) {
    cnt_.puts++;
    if (cap_ == 0) return;
    auto it = pos_.find(key);
    if (it != pos_.end()) {
        it->second->second = value;
        touch(it);
        return;
    }
    if (order_.size() == cap_) {
        auto [k, v] = order_.back();
        pos_.erase(k);
        order_.pop_back();
        cnt_.evictions++;
        if (g_on_evict_key) g_on_evict_key(k);
    }
    order_.emplace_front(key, value);
    pos_[key] = order_.begin();
}

void LRUCacheIter::estimateMemory(size_t& theoretical, size_t& actual, size_t& overhead) const {
    theoretical = cap_ * sizeof(Node);
    actual = order_.size() * sizeof(Node);
    size_t list_over = order_.size() * (sizeof(void*) * 2);
    size_t map_over = pos_.size() * (sizeof(void*) * 2 + sizeof(int));
    overhead = list_over + map_over;
}

LRUCacheRec::LRUCacheRec(size_t cap) : cap_(cap) {}
LRUCacheRec::~LRUCacheRec(){ freeList(head_); }

void LRUCacheRec::freeList(Node* n){ if(!n) return; freeList(n->next); delete n; deallocations_++; }

std::optional<int> LRUCacheRec::get(int key) {
    cnt_.gets++;
    auto r = getRec(nullptr, head_, key);
    if (r) cnt_.hits++; else cnt_.misses++;
    return r;
}

std::optional<int> LRUCacheRec::getRec(Node* prev, Node* cur, int key) {
    if (!cur) return std::nullopt;
    if (cur->key == key) {
        if (prev) { prev->next = cur->next; cur->next = head_; head_ = cur; }
        return cur->val;
    }
    return getRec(cur, cur->next, key);
}

void LRUCacheRec::put(int key, int value) {
    cnt_.puts++;
    if (putUpdateRec(nullptr, head_, key, value)) return;
    if (cap_ == 0) return;
    if (sz_ == cap_) {
        bool removed = false;
        head_ = removeTailRec(head_, removed);
        if (removed && sz_>0) { cnt_.evictions++; sz_--; }
    }
    Node* n = new Node{key, value, head_};
    allocations_++;
    head_ = n;
    sz_++;
}

bool LRUCacheRec::putUpdateRec(Node* prev, Node* cur, int key, int value) {
    if (!cur) return false;
    if (cur->key == key) {
        cur->val = value;
        if (prev) { prev->next = cur->next; cur->next = head_; head_ = cur; }
        return true;
    }
    return putUpdateRec(cur, cur->next, key, value);
}

LRUCacheRec::Node* LRUCacheRec::removeTailRec(Node* cur, bool& removed) {
    if (!cur) { removed = false; return nullptr; }
    if (!cur->next) {
        if (g_on_evict_key) g_on_evict_key(cur->key);
        delete cur; deallocations_++; removed = true; return nullptr;
    }
    cur->next = removeTailRec(cur->next, removed);
    return cur;
}

void LRUCacheRec::estimateMemory(size_t& theoretical, size_t& actual, size_t& overhead) const {
    theoretical = cap_ * sizeof(Node);
    actual = sz_ * sizeof(Node);
    overhead = sz_ * sizeof(void*);
}
