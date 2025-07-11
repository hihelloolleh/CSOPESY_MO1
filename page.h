#pragma once
#include <string>

class Page {
public:
    int processId;
    int pageNumber;
    bool valid;
    bool dirty;
    int frameIndex;
    bool inMemory = false;
    int entryNumber = -1; // can be frame index or backing store index


    Page(int pid, int pageNum)
        : processId(pid), pageNumber(pageNum),
        valid(false), dirty(false), frameIndex(-1) {
    }

    std::string toString() const {
        std::string location = inMemory ? "MEM" : "DISK";
        return "[P" + std::to_string(processId) + " Pg#" + std::to_string(pageNumber) +
            " -> " + location + ":" + std::to_string(entryNumber) + "]";
    }
};
