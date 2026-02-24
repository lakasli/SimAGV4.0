#include "trace_ring_buffer.hpp"

namespace simagv::l2 {

TraceRingBuffer::TraceRingBuffer(size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity), buffer_(capacity_), writeIndex_(0), wrapped_(false)
{
}

void TraceRingBuffer::push(TraceRecord record)
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_[writeIndex_] = std::move(record);
    writeIndex_ = (writeIndex_ + 1) % capacity_;
    if (writeIndex_ == 0) {
        wrapped_ = true;
    }
}

std::vector<TraceRecord> TraceRingBuffer::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TraceRecord> out;
    if (!wrapped_) {
        out.insert(out.end(), buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(writeIndex_));
        return out;
    }
    out.insert(out.end(), buffer_.begin() + static_cast<std::ptrdiff_t>(writeIndex_), buffer_.end());
    out.insert(out.end(), buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(writeIndex_));
    return out;
}

} // namespace simagv::l2

