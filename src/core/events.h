#pragma once
// Events the audio thread reports to the game (spec audio-engine "Lock-free message
// boundary", master-clock "The game reads audio time without locks"): plain data,
// pushed through a lock-free FIFO, each carrying its exact master-clock sample.

#include <cstdint>
#include <limits>  // CHOC's FIFO header uses std::numeric_limits without including it

#include "choc/containers/choc_SingleReaderSingleWriterFIFO.h"

namespace stubby {

struct AudioEvent {
    enum class Kind : uint8_t { Tick, Late };
    Kind kind = Kind::Tick;
    int handle = -1;       // the module (Tick); -1 for engine events
    uint64_t sample = 0;   // when it happened (Tick) or was applied (Late)
    int64_t value = 0;     // tick index (Tick); samples missed (Late)
    uint32_t action = 0;   // scheduled action id (Late)
};

/// Audio thread side of the event FIFO. A full FIFO drops events and counts them.
class EventSink {
public:
    explicit EventSink(size_t capacity) { fifo_.reset(capacity); }
    void push(const AudioEvent &e) {
        if (!fifo_.push(e)) ++dropped_;
    }
    bool pop(AudioEvent &e) { return fifo_.pop(e); }
    uint64_t dropped() const { return dropped_; }
private:
    choc::fifo::SingleReaderSingleWriterFIFO<AudioEvent> fifo_;
    uint64_t dropped_ = 0;
};

}  // namespace stubby
