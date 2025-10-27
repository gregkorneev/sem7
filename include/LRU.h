#pragma once
#include "CacheBase.h"
#include <list>
#include <unordered_map>
#include <optional>

/**
 * ===================== ИТЕРАЦИОННЫЙ LRU =====================
 * Классическая реализация: unordered_map + двусвязный список.
 *   - Список хранит пары (key,value), голова — MRU, хвост — LRU.
 *   - Хеш-таблица даёт O(1) доступ к итератору списка для key.
 */
class LRUCacheIter : public ICache {
public:
    explicit LRUCacheIter(size_t cap);
    void put(int key, int value) override;
    std::optional<int> get(int key) override;
    size_t size() const override { return order_.size(); }
    size_t capacity() const override { return cap_; }
    const OpCounters& counters() const override { return cnt_; }

private:
    using Node = std::pair<int,int>; // (key,value)

    size_t cap_;
    std::list<Node> order_; // front — MRU, back — LRU
    std::unordered_map<int, std::list<Node>::iterator> pos_;
    OpCounters cnt_;

    // Перемещение узла в голову (MRU)
    void touch(std::unordered_map<int, std::list<Node>::iterator>::iterator it);
};

/**
 * ===================== РЕКУРСИВНЫЙ LRU =====================
 * Односвязный список без хеш-таблицы (для демонстрации рекурсивного подхода).
 *   - Голова списка — MRU (последний использованный).
 *   - get/put проходят список РЕКУРСИВНО.
 *   - При get найденный узел "поднимается" в голову.
 *   - При переполнении удаляем хвост (самый LRU) рекурсивной процедурой.
 * Это НЕ оптимально по асимптотике (O(n)), но показывает идею рекурсии.
 */
class LRUCacheRec : public ICache {
public:
    explicit LRUCacheRec(size_t cap);
    ~LRUCacheRec();

    void put(int key, int value) override;
    std::optional<int> get(int key) override;
    size_t size() const override { return sz_; }
    size_t capacity() const override { return cap_; }
    const OpCounters& counters() const override { return cnt_; }

private:
    struct Node { int key, val; Node* next; };
    Node* head_ = nullptr; // MRU
    size_t cap_;
    size_t sz_ = 0;
    OpCounters cnt_;

    // рекурсивный поиск с "подъёмом" найденного узла в голову
    std::optional<int> getRec(Node* prev, Node* cur, int key);

    // рекурсивная попытка обновить существующий ключ (подняв в голову);
    // возвращает флаг "обновили/нашли?"
    bool putUpdateRec(Node* prev, Node* cur, int key, int value);

    // рекурсивное удаление хвоста; возвращает новый head
    Node* removeTailRec(Node* cur, bool& removed);

    // утилита для освобождения списка
    void freeList(Node* n);
};
