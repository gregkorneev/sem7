#include "LFU.h"
#include "CacheBase.h"

LFUCacheIter::LFUCacheIter(size_t cap) : cap_(cap) {}

void LFUCacheIter::touch(std::unordered_map<int, std::list<Node>::iterator>::iterator it) {
    auto node = *(it->second);
    int f = node.freq;
    buckets_[f].erase(it->second);
    if (buckets_[f].empty()) {
        buckets_.erase(f);
        if (minFreq_ == f) minFreq_++;
    }
    node.freq++;
    buckets_[node.freq].push_front(node);
    it->second = buckets_[node.freq].begin();
}

void LFUCacheIter::evictOne() {
    if (sz_ == 0) return;
    auto& lst = buckets_[minFreq_];
    auto victim = lst.back();
    if (g_on_evict_key) g_on_evict_key(victim.key);
    pos_.erase(victim.key);
    lst.pop_back();
    if (lst.empty()) buckets_.erase(minFreq_);
    sz_--;
}

std::optional<int> LFUCacheIter::get(int key) {
    cnt_.gets++;
    auto it = pos_.find(key);
    if (it == pos_.end()) { cnt_.misses++; return std::nullopt; }
    touch(it);
    cnt_.hits++;
    return it->second->val;
}

void LFUCacheIter::put(int key, int value) {
    cnt_.puts++;
    if (cap_ == 0) return;
    auto it = pos_.find(key);
    if (it != pos_.end()) { it->second->val = value; touch(it); return; }
    if (sz_ == cap_) { evictOne(); cnt_.evictions++; }
    Node n{key, value, 1};
    buckets_[1].push_front(n);
    pos_[key] = buckets_[1].begin();
    minFreq_ = 1;
    sz_++;
}

void LFUCacheIter::estimateMemory(size_t& theoretical, size_t& actual, size_t& overhead) const {
    theoretical = cap_ * sizeof(Node);
    actual = sz_ * sizeof(Node);
    size_t buckets_over = 0;
    for (const auto& kv : buckets_) buckets_over += kv.second.size() * (sizeof(void*) * 2);
    size_t map_over = pos_.size() * (sizeof(void*) * 2 + sizeof(int));
    overhead = buckets_over + map_over;
}

LFUCacheRec::LFUCacheRec(size_t cap) : cap_(cap) {}
LFUCacheRec::~LFUCacheRec(){ freeList(head_); }
void LFUCacheRec::freeList(Node* n){ if(!n) return; freeList(n->next); delete n; deallocations_++; }

std::optional<int> LFUCacheRec::get(int key) {
    cnt_.gets++;
    auto v = getRec(head_, key);
    if (v) cnt_.hits++; else cnt_.misses++;
    return v;
}

std::optional<int> LFUCacheRec::getRec(Node* cur, int key) {
    if (!cur) return std::nullopt;
    if (cur->key == key) { cur->freq++; return cur->val; }
    return getRec(cur->next, key);
}

bool LFUCacheRec::putUpdateRec(Node* cur, int key, int value) {
    if (!cur) return false;
    if (cur->key == key) { cur->val = value; cur->freq++; return true; }
    return putUpdateRec(cur->next, key, value);
}

std::pair<LFUCacheRec::Node*, LFUCacheRec::Node*>
LFUCacheRec::findMinPrevRec(Node* prev, Node* cur, Node* bestPrev, Node* best) {
    if (!cur) return {bestPrev, best};
    if (!best || cur->freq < best->freq) { bestPrev = prev; best = cur; }
    return findMinPrevRec(cur, cur->next, bestPrev, best);
}

void LFUCacheRec::put(int key, int value) {
    cnt_.puts++;
    if (cap_ == 0) return;
    if (putUpdateRec(head_, key, value)) return;
    if (sz_ == cap_) {
        auto [pMin, minNode] = findMinPrevRec(nullptr, head_, nullptr, nullptr);
        if (minNode) {
            if (g_on_evict_key) g_on_evict_key(minNode->key);
            if (pMin) pMin->next = minNode->next; else head_ = minNode->next;
            delete minNode; deallocations_++;
            cnt_.evictions++; sz_--;
        }
    }
    Node* n = new Node{key, value, 1, head_};
    allocations_++;
    head_ = n; sz_++;
}

void LFUCacheRec::estimateMemory(size_t& theoretical, size_t& actual, size_t& overhead) const {
    theoretical = cap_ * sizeof(Node);
    actual = sz_ * sizeof(Node);
    overhead = sz_ * sizeof(void*);
}
