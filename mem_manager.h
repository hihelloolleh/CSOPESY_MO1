#pragma once
#include <vector>
#include <unordered_map>
#include <queue>
#include <string>
#include <cstdint>
#include <mutex>
#include <future>
#include "pcb.h"
#include "config.h"

// Forward-declare Process to avoid circular dependency
struct Process;

class MemoryManager {
public:
    MemoryManager(const Config& config);
    ~MemoryManager();

    // Process lifecycle management
    bool createProcess(const Process& proc);
    void removeProcess(int pid);
    bool isProcessActive(int pid);

    // Memory access interface (used by instructions)
    bool readMemory(int pid, uint16_t address, uint16_t& value);
    bool writeMemory(int pid, uint16_t address, uint16_t value);
    
    // Checks if a page is valid. If not, pages it in.
    // Returns true if a page fault occurred, false otherwise.
    bool touchPage(int pid, uint16_t address);

    // Snapshot and reporting
    void snapshotMemory(uint64_t tick);
    void flushAsyncWrites();

    std::tuple<size_t, size_t> getMemoryUsageStats();


    const std::unordered_map<int, PCB>& getProcessTable() const {
        return processTable;
    }


    std::unique_lock<std::mutex> lockManager() {
        return std::unique_lock<std::mutex>(manager_mutex);
    }

    size_t getPageInCount() const { return pageFaults; }
    size_t getPageOutCount() const { return pageEvictions; }

private:
    // Core memory components
    size_t totalMemory;
    size_t frameSize;
    size_t totalFrames;
    size_t max_pages_per_process;
    size_t total_committed_memory = 0;
    std::vector<std::vector<uint8_t>> physicalMemory;
    std::vector<bool> frameOccupied;
    std::string backing_store_filename;

    void writePageToBackingStore(int pid, size_t pageNum, const std::vector<uint8_t>& data);
    void readPageFromBackingStore(int pid, size_t pageNum, std::vector<uint8_t>& data);

    // Page replacement (FIFO)
    std::queue<size_t> frameQueue;

    // Process and Page management
    std::unordered_map<int, PCB> processTable;
    std::unordered_map<size_t, std::pair<int, size_t>> frameToPageMap;

    // Statistics
    size_t pageFaults = 0;
    size_t pageEvictions = 0;

    // Paging mechanism
    size_t getFreeFrameOrEvict();
    void pageIn(PCB& pcb, Page& page);
    void pageOut(size_t frameIndex);
    
    // Thread safety and async operations
    std::mutex manager_mutex;
    std::mutex snapshot_mutex;
    std::string last_snapshot_signature;
    std::vector<std::future<void>> background_tasks;
};