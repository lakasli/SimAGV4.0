#pragma once

#include "l2_types.hpp"

#include <cstddef>
#include <mutex>
#include <vector>

namespace simagv::l2 {

class TraceRingBuffer final {
public:
    explicit TraceRingBuffer(size_t capacity);

    void push(TraceRecord record);
    std::vector<TraceRecord> snapshot() const;

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<TraceRecord> buffer_;
    size_t writeIndex_;
    bool wrapped_;
};

} // namespace simagv::l2

