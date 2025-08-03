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
    
    // --- PHASE 3 ---
    // Checks if a page is valid. If not, pages it in.
    // Returns true if a page fault occurred, false otherwise.
    bool touchPage(int pid, uint16_t address);

    // Snapshot and reporting
    void snapshotMemory(int tick);
    void flushAsyncWrites();

    std::tuple<size_t, size_t> getMemoryUsageStats();


    const std::unordered_map<int, PCB>& getProcessTable() const {
        return processTable;
    }


    std::unique_lock<std::mutex> lockManager() {
        return std::unique_lock<std::mutex>(manager_mutex);
    }

private:
    // Core memory components
    size_t totalMemory;
    size_t frameSize;
    size_t totalFrames;
    std::vector<std::vector<uint8_t>> physicalMemory;
    std::vector<bool> frameOccupied;

    // Page replacement (FIFO)
    std::queue<int> frameQueue;

    // Process and Page management
    std::unordered_map<int, PCB> processTable;
    std::unordered_map<int, std::pair<int, int>> frameToPageMap;

    // Statistics
    int pageFaults = 0;
    int pageEvictions = 0;

    // Paging mechanism
    int getFreeFrameOrEvict();
    void pageIn(PCB& pcb, Page& page);
    void pageOut(int frameIndex);
    
    // Thread safety and async operations
    std::mutex manager_mutex;
    std::mutex snapshot_mutex;
    std::string last_snapshot_signature;
    std::vector<std::future<void>> background_tasks;

  
};