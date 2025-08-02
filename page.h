#pragma once
#include <string>
#include <cstdint> // For uint64_t

class Page {
public:
    int processId;
    int pageNumber;
    bool valid;      // Is the page in physical memory?
    bool dirty;      // Has the page been modified since being loaded?
    int frameIndex;  // Its location in physical memory if valid=true
    bool inMemory;   // Redundant with 'valid', but can be useful for clarity
    
    // --- ADDED MEMBER ---
    // Used by LRU/LFU replacement algorithms to track usage.
    // We'll add it now for future compatibility and to fix the current code.
    uint64_t lastAccessed = 0;

    Page(int pid, int pageNum)
        : processId(pid), pageNumber(pageNum),
        valid(false), dirty(false), frameIndex(-1), inMemory(false) {
    }

    // A simple to_string method can be useful for debugging
    std::string toString() const {
        std::string location = valid ? ("Frame " + std::to_string(frameIndex)) : "Disk";
        return "[P" + std::to_string(processId) + " Pg#" + std::to_string(pageNumber) +
            " -> " + location + (dirty ? " (Dirty)" : "") + "]";
    }
};