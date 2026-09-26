// MicroHydros - fix ringbuffert
//
// Anvands pa tva stallen: som fonster for trendberakningen och som kö for
// telemetriramar nar natet ligger nere. Kapaciteten ar en mallparameter sa att
// minnet allokeras statiskt vid lankning. Ingen malloc, ingen fragmentering,
// ingen risk att enheten dor efter tre veckor for att heapen har trasats sonder.
//
// Nar bufferten ar full skrivs det aldsta vardet over. For trendfonstret ar det
// exakt vad vi vill. For telemetrikön ar det ett medvetet val: nya matvarden ar
// mer varda an gamla, och forlusten rapporteras i falten dropped_frames.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mh {

template <typename T, size_t Capacity>
class RingBuffer {
public:
    static_assert(Capacity > 0, "RingBuffer behover minst ett element");

    RingBuffer() : head_(0), count_(0), overwritten_(0) {}

    size_t capacity() const { return Capacity; }
    size_t size() const { return count_; }
    bool   empty() const { return count_ == 0; }
    bool   full() const { return count_ == Capacity; }

    /// Antal element som har trangts undan sedan start. Rapporteras i telemetrin
    /// sa att man i efterhand ser om enheten tappade data under ett natavbrott.
    uint32_t overwritten() const { return overwritten_; }

    void push(const T& item) {
        buffer_[head_] = item;
        head_ = (head_ + 1) % Capacity;
        if (count_ < Capacity) {
            ++count_;
        } else {
            ++overwritten_;
        }
    }

    /// Hamtar aldsta elementet utan att ta bort det.
    bool peek(T& out) const {
        if (count_ == 0) return false;
        out = buffer_[tailIndex()];
        return true;
    }

    /// Tar bort aldsta elementet.
    bool pop(T& out) {
        if (count_ == 0) return false;
        out = buffer_[tailIndex()];
        --count_;
        return true;
    }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

    /// Indexering fran aldsta (0) till nyaste (size()-1).
    const T& at(size_t i) const { return buffer_[(tailIndex() + i) % Capacity]; }
    T&       at(size_t i) { return buffer_[(tailIndex() + i) % Capacity]; }

    /// Nyaste elementet. Odefinierat om bufferten ar tom, kolla size() forst.
    const T& newest() const { return buffer_[(head_ + Capacity - 1) % Capacity]; }

private:
    size_t tailIndex() const { return (head_ + Capacity - count_) % Capacity; }

    T        buffer_[Capacity];
    size_t   head_;
    size_t   count_;
    uint32_t overwritten_;
};

} // namespace mh
