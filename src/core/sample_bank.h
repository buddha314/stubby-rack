#pragma once
// Sample data for the sampler (spec audio-engine "All audio goes through the rack";
// design D5): one-shot sounds are decoded on the main thread into mono float buffers
// and registered here before any command refers to them. The bank is append-only and
// its entry table is sized at construction, so the audio thread reads entries without
// locks: an entry is written before the command batch naming it is published (the
// FIFO's release/acquire orders the two), and is never changed afterwards.

#include <cstdint>
#include <memory>
#include <vector>

namespace stubby {

class SampleBank {
public:
    struct Entry {
        const float *data = nullptr;
        uint32_t length = 0;
        double sample_rate = 48000.0;
    };

    explicit SampleBank(int capacity = 1024) : entries_(static_cast<size_t>(capacity)) {}

    /// Main thread: registers mono samples (-1..1) at `sample_rate`. Returns the id, or
    /// -1 when the bank is full or the buffer is empty.
    int add(std::vector<float> samples, double sample_rate) {
        if (samples.empty() || count_ >= static_cast<int>(entries_.size())) return -1;
        owned_.push_back(std::make_unique<std::vector<float>>(std::move(samples)));
        Entry &e = entries_[static_cast<size_t>(count_)];
        e.data = owned_.back()->data();
        e.length = static_cast<uint32_t>(owned_.back()->size());
        e.sample_rate = sample_rate;
        return count_++;
    }

    /// Any thread: the entry for `id`, or nullptr.
    const Entry *get(int id) const {
        return id >= 0 && id < static_cast<int>(entries_.size()) && entries_[static_cast<size_t>(id)].data
                   ? &entries_[static_cast<size_t>(id)]
                   : nullptr;
    }
    int size() const { return count_; }

private:
    std::vector<Entry> entries_;
    std::vector<std::unique_ptr<std::vector<float>>> owned_;
    int count_ = 0;
};

}  // namespace stubby
