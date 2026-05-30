#pragma once
#include <windows.h>
#include <vector>
#include <cstdint>
#include <optional>
#include "video_frame.h"

// Lock-free single-producer / single-consumer ring buffer for VideoFrames.
// Producer: USB capture thread.  Consumer: DirectShow deliver thread.
class FrameRingBuffer {
public:
    explicit FrameRingBuffer(size_t capacity) : m_capacity(capacity) {
        m_slots.resize(capacity);
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
    }

    // Called by producer. Returns false if buffer is full (frame dropped).
    bool push(VideoFrame&& frame) {
        const size_t head = m_head.load(std::memory_order_relaxed);
        const size_t next = (head + 1) % m_capacity;
        if (next == m_tail.load(std::memory_order_acquire))
            return false;   // full – drop oldest would require two-phase swap
        m_slots[head] = std::move(frame);
        m_head.store(next, std::memory_order_release);
        return true;
    }

    // Called by consumer. Returns empty optional if buffer is empty.
    std::optional<VideoFrame> pop() {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return std::nullopt;
        VideoFrame frame = std::move(m_slots[tail]);
        m_tail.store((tail + 1) % m_capacity, std::memory_order_release);
        return frame;
    }

    bool empty() const {
        return m_head.load(std::memory_order_acquire) ==
               m_tail.load(std::memory_order_acquire);
    }

private:
    size_t                    m_capacity;
    std::vector<VideoFrame>   m_slots;
    std::atomic<size_t>       m_head;
    std::atomic<size_t>       m_tail;
};
