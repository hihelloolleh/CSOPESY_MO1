#pragma once
#include <vector>
#include <unordered_map>
#include <queue>
#include <fstream>
#include <cstdint>
#include <mutex>
#include <future>
#include "page.h"
#include "pcb.h"
#include "process.h"
#include "config.h"

class MemoryManager {
public:
    MemoryManager(const Config& config);

    bool createProcess(const Process& proc, size_t memoryRequired);
    void removeProcess(int pid);

    bool readMemory(int pid, uint16_t address, uint16_t& value);
    bool writeMemory(int pid, uint16_t address, uint16_t value);

    Process* getProcess(int pid);

    void showProcessSMI();
    void showVMStat();
    void snapshotMemory(int quantumCycle);
    void flushAsyncWrites();

    mutable std::mutex snapshot_mutex;
    std::vector<std::string> snapshot_log;
    std::string last_snapshot_signature;
    std::vector<std::future<void>> background_tasks;


private:
    size_t totalMemory;
    size_t frameSize;
    size_t totalFrames;

    std::vector<std::vector<uint8_t>> physicalMemory;
    std::vector<bool> frameOccupied;
    std::queue<int> frameQueue;

    std::unordered_map<int, PCB> processTable;

    std::fstream backingStore;
    const std::string backingStoreFile = "csopesy-backing-store.txt";

    int pageFaults = 0;
    int pageEvictions = 0;

    int getFreeFrameOrEvict();
    void pageIn(PCB& pcb, Page& page);
    void pageOut(int frameIndex);
    
};



