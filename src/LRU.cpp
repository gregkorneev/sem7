#include "LRU.h"
#include <cassert>

// ====================== LRU ITER ======================
LRUCacheIter::LRUCacheIter(size_t cap) : cap_(cap) {}

void LRUCacheIter::touch(std::unordered_map<int, std::list<Node>::iterator>::iterator it) {
    // Перемещаем найденный элемент в голову списка (стал MRU)
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
        // вытесняем самый старый (LRU — в хвосте)
        auto [k, v] = order_.back();
        pos_.erase(k);
        order_.pop_back();
        cnt_.evictions++;
    }
    order_.emplace_front(key, value);
    pos_[key] = order_.begin();
}

// ====================== LRU REC ======================

LRUCacheRec::LRUCacheRec(size_t cap) : cap_(cap) {}
LRUCacheRec::~LRUCacheRec(){ freeList(head_); }

void LRUCacheRec::freeList(Node* n){ if(!n) return; freeList(n->next); delete n; }

std::optional<int> LRUCacheRec::get(int key) {
    cnt_.gets++;
    auto r = getRec(nullptr, head_, key);
    if (r) cnt_.hits++; else cnt_.misses++;
    return r;
}

std::optional<int> LRUCacheRec::getRec(Node* prev, Node* cur, int key) {
    if (!cur) return std::nullopt;
    if (cur->key == key) {
        // Поднять найденный узел в голову (MRU)
        if (prev) {
            prev->next = cur->next;
            cur->next = head_;
            head_ = cur;
        }
        return cur->val;
    }
    return getRec(cur, cur->next, key);
}

void LRUCacheRec::put(int key, int value) {
    cnt_.puts++;
    // Пытаемся обновить существующий ключ и поднять его в голову
    if (putUpdateRec(nullptr, head_, key, value)) return;

    // Не нашли — вставим в голову
    if (cap_ == 0) return;
    if (sz_ == cap_) {
        bool removed = false;
        head_ = removeTailRec(head_, removed);
        if (removed && sz_>0) { cnt_.evictions++; sz_--; }
    }
    Node* n = new Node{key, value, head_};
    head_ = n;
    sz_++;
}

bool LRUCacheRec::putUpdateRec(Node* prev, Node* cur, int key, int value) {
    if (!cur) return false;
    if (cur->key == key) {
        cur->val = value;
        // поднять в голову
        if (prev) {
            prev->next = cur->next;
            cur->next = head_;
            head_ = cur;
        }
        return true;
    }
    return putUpdateRec(cur, cur->next, key, value);
}

LRUCacheRec::Node* LRUCacheRec::removeTailRec(Node* cur, bool& removed) {
    if (!cur) { removed = false; return nullptr; }
    if (!cur->next) {
        // это хвост — удаляем
        delete cur;
        removed = true;
        return nullptr;
    }
    cur->next = removeTailRec(cur->next, removed);
    return cur;
}
