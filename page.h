#pragma once
#include <string>
#include <cstdint>
#include <limits>   
#include <sstream>

class Page {
public:
    int processId;
    size_t pageNumber;
    bool valid;      // Is the page in physical memory?
    bool dirty;      // Has the page been modified since being loaded?
    size_t frameIndex;  // Its location in physical memory if valid=true
    bool inMemory;   // Redundant with 'valid', but can be useful for clarity
    bool onBackingStore;
   
    // Used by LRU/LFU replacement algorithms to track usage.
    uint64_t lastAccessed = 0;
    static const size_t INVALID_FRAME = std::numeric_limits<size_t>::max();


    Page(int pid, int pageNum)
        : processId(pid), pageNumber(pageNum),
        valid(false), dirty(false), frameIndex(INVALID_FRAME), inMemory(false), onBackingStore(false) {
    }

    // A simple to_string method can be useful for debugging
    std::string toString() const {
        std::stringstream ss;
        std::string location = valid ? ("Frame " + std::to_string(frameIndex)) : "Disk";
        ss << "[P" << processId << " Pg#" << pageNumber
            << " -> " << location << (dirty ? " (Dirty)" : "")
            << (onBackingStore ? " (On-Disk)" : "") << "]";
        return ss.str();
    }
};