#pragma once
#include "FreeRTOS.h"
#include <cstddef>
#include <cstring>

struct TestQueue {
    unsigned int depth;
    unsigned int itemSize;
    unsigned int count;
    unsigned int head;
    unsigned int tail;
    unsigned char storage[8][64];
};

using QueueHandle_t = TestQueue*;

inline QueueHandle_t xQueueCreate(unsigned int depth, unsigned int itemSize) {
    static TestQueue q{};
    q = {};
    q.depth = depth > 8 ? 8 : depth;
    q.itemSize = itemSize > 64 ? 64 : itemSize;
    return &q;
}

inline BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t) {
    if (!q || q->count >= q->depth) return pdFALSE;
    std::memcpy(q->storage[q->tail], item, q->itemSize);
    q->tail = (q->tail + 1) % q->depth;
    q->count++;
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t) {
    if (!q || q->count == 0) return pdFALSE;
    std::memcpy(item, q->storage[q->head], q->itemSize);
    q->head = (q->head + 1) % q->depth;
    q->count--;
    return pdTRUE;
}
