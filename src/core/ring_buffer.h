#pragma once
// 定容环形历史缓冲（01-core §7）：满则覆盖最旧，snapshot 按时间序（旧→新）展开拷出。
// 单线程使用（仅采集线程写、拷出时持 hub 锁），无需内部加锁。

#include <cstddef>
#include <utility>
#include <vector>

namespace hwmon {

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) : buf_(capacity) {}

    void push(T v) {                       // 满则覆盖最旧
        if (buf_.empty()) return;
        if (count_ < buf_.size()) {
            buf_[(head_ + count_) % buf_.size()] = std::move(v);
            ++count_;
        } else {
            buf_[head_] = std::move(v);
            head_ = (head_ + 1) % buf_.size();
        }
    }

    size_t size() const { return count_; }

    // 按 时间序 展开拷出（旧→新）；O(n)
    std::vector<T> snapshot() const {
        std::vector<T> out;
        out.reserve(count_);
        for (size_t i = 0; i < count_; ++i)
            out.push_back(buf_[(head_ + i) % buf_.size()]);
        return out;
    }

private:
    std::vector<T> buf_;
    size_t head_ = 0, count_ = 0;   // head_ = 最旧元素下标
};

}  // namespace hwmon
