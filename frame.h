#ifndef FRAME_H
#define FRAME_H

#include <vector>
#include <cstdint>

// Represents a single physical memory frame in our emulated system.
struct Frame {
    // Each frame has its own block of memory.
    std::vector<uint8_t> data;

    // Constructor to initialize the frame with a specific size.
    Frame(size_t frameSize) : data(frameSize, 0) {}

    // Default constructor for convenience.
    Frame() = default;
};

#endif // FRAME_H