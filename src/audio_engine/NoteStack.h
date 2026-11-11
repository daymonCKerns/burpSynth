#pragma once

// The notes currently held down, oldest first. The synth is monophonic and
// always plays the most recently pressed note that is still held, so
// releasing a note falls back to the one before it.
//
// Fixed capacity: when full, the oldest note is dropped.
//
// Header-only: every function is defined inline in this file.

#include <array>
#include <cstddef>

namespace burp {

class NoteStack {
public:
    static constexpr int kCapacity = 16;

    void clear();

    // Adds a held note. `semitones` is the pitch including any tuning offset.
    // Pressing a key that is already held moves it to the top.
    void push(int key, double semitones);

    // Removes a key. Returns false if it was not held.
    bool remove(int key);

    bool empty() const;
    int size() const;
    int topKey() const;          // most recent held key; -1 if empty
    double topSemitones() const; // its pitch; 0 if empty

private:
    struct Note {
        int key = -1;
        double semitones = 0.0;
    };

    // Removes the note at `index`, shifting the newer ones down.
    void erase(int index);

    std::array<Note, kCapacity> notes_ {};
    int size_ = 0;
};

inline void NoteStack::clear()
{
    size_ = 0;
}

inline void NoteStack::push(int key, double semitones)
{
    remove(key);
    if (size_ == kCapacity) {
        erase(0);
    }
    notes_[static_cast<std::size_t>(size_)] = Note {key, semitones};
    ++size_;
}

inline bool NoteStack::remove(int key)
{
    for (int i = 0; i < size_; ++i) {
        if (notes_[static_cast<std::size_t>(i)].key == key) {
            erase(i);
            return true;
        }
    }
    return false;
}

inline bool NoteStack::empty() const
{
    return size_ == 0;
}

inline int NoteStack::size() const
{
    return size_;
}

inline int NoteStack::topKey() const
{
    return size_ > 0 ? notes_[static_cast<std::size_t>(size_ - 1)].key : -1;
}

inline double NoteStack::topSemitones() const
{
    return size_ > 0 ? notes_[static_cast<std::size_t>(size_ - 1)].semitones : 0.0;
}

inline void NoteStack::erase(int index)
{
    for (int i = index + 1; i < size_; ++i) {
        notes_[static_cast<std::size_t>(i - 1)] = notes_[static_cast<std::size_t>(i)];
    }
    --size_;
}

} // namespace burp
