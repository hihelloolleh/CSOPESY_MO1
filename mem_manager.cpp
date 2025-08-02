#include "mem_manager.h"
#include "pcb.h"
#include "page.h"
#include "process.h"
#include "config.h"
#include "shared_globals.h" 
#include <iostream>
#include <iomanip>
#include <cstring>
#include <sstream>    
#include <fstream>    
#include <ctime>      
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <map>

namespace fs = std::filesystem;

MemoryManager::MemoryManager(const Config& config)
    : totalMemory(config.max_overall_mem),
    frameSize(config.mem_per_frame) {

    totalFrames = static_cast<int>(totalMemory / frameSize);

    std::cout << "[MemManager] Initializing with " << totalFrames << " frames of " << frameSize << " bytes each." << std::endl;

    physicalMemory.resize(totalFrames, std::vector<uint8_t>(frameSize, 0));
    frameOccupied.resize(totalFrames, false);
    backingStore.open(backingStoreFile, std::ios::in | std::ios::out | std::ios::trunc);
}

bool MemoryManager::createProcess(const Process& proc) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    int pid = proc.id;
    const std::string& name = proc.name;
    size_t memoryRequired = proc.memory_required;

    if (processTable.find(pid) != processTable.end()) return false;

    size_t pagesNeeded = (memoryRequired + frameSize - 1) / frameSize;
    size_t freeFrames = 0;
    for (bool used : frameOccupied) {
        if (!used) ++freeFrames;
    }

    // This check might be simplified as the page-in mechanism can handle full memory.
    // However, it's a good initial guard.
    if (freeFrames < 1 && pagesNeeded > 0) { // Only reject if there are literally no frames to start with.
        return false; 
    }

    PCB pcb(pid, name);
    pcb.process = proc;

    for (int pageNum : proc.insPages) {
        Page p(pid, pageNum);
        pcb.addPage(std::move(p));
    }
    for (int pageNum : proc.varPages) {
        Page p(pid, pageNum);
        pcb.addPage(std::move(p));
    }
    
    std::cout << "[MemManager] Allocated page table for process " << pid << " (" << name << ") with " << pcb.pageTable.size() << " virtual pages." << std::endl;
    processTable[pid] = std::move(pcb);
    return true;
}

void MemoryManager::removeProcess(int pid) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    auto it = processTable.find(pid);
    if (it == processTable.end()) return;

    PCB& pcb = it->second;
    for (auto& page : pcb.pageTable) {
        if (page.valid && page.frameIndex >= 0 && static_cast<size_t>(page.frameIndex) < frameOccupied.size()) {
            frameOccupied[page.frameIndex] = false;
            page.valid = false;
            page.frameIndex = -1;
        }
    }
    processTable.erase(it);
}

bool MemoryManager::readMemory(int pid, uint16_t address, uint16_t& value) {
    std::lock_guard<std::mutex> lock(manager_mutex);
    auto it = processTable.find(pid);
    if (it == processTable.end()) return false;
    PCB& pcb = it->second;
    size_t pageNum = address / frameSize;
    size_t offset = address % frameSize;
    if (pageNum >= pcb.pageTable.size()) return false;
    Page& page = pcb.pageTable[pageNum];
    if (!page.valid) {
        pageIn(pcb, page);
    }
    if (offset + sizeof(uint16_t) > frameSize) return false;
    std::memcpy(&value, &physicalMemory[page.frameIndex][offset], sizeof(uint16_t));
    return true;
}

bool MemoryManager::writeMemory(int pid, uint16_t address, uint16_t value) {
    std::lock_guard<std::mutex> lock(manager_mutex);
    auto it = processTable.find(pid);
    if (it == processTable.end()) return false;
    PCB& pcb = it->second;
    size_t pageNum = address / frameSize;
    size_t offset = address % frameSize;
    if (pageNum >= pcb.pageTable.size()) return false;
    Page& page = pcb.pageTable[pageNum];
    if (!page.valid) {
        pageIn(pcb, page);
    }
    if (offset + sizeof(uint16_t) > frameSize) return false;
    std::memcpy(&physicalMemory[page.frameIndex][offset], &value, sizeof(uint16_t));
    page.dirty = true;
    return true;
}

Process* MemoryManager::getProcess(int pid) {
    std::lock_guard<std::mutex> lock(manager_mutex);
    auto it = processTable.find(pid);
    if (it == processTable.end()) return nullptr;
    return &it->second.process;
}

void MemoryManager::pageIn(PCB& pcb, Page& page) {
    int frameIndex = getFreeFrameOrEvict();
    if (frameIndex == -1) {
        std::cerr << "[MemManager] CRITICAL: No frames available to page in." << std::endl;
        return;
    }
    frameOccupied[frameIndex] = true; 
    frameQueue.push(frameIndex); 
    std::fill(physicalMemory[frameIndex].begin(), physicalMemory[frameIndex].end(), 0);
    page.frameIndex = frameIndex;
    page.valid = true;
    page.inMemory = true;
    pageFaults++;
    std::cout << "[MemManager] Page fault for P" << pcb.getPid() << " Page " << page.pageNumber << ". Loaded into Frame " << frameIndex << "." << std::endl;
}

void MemoryManager::pageOut(int frameIndex) {
    for (auto& entry : processTable) {
        int pid = entry.first;
        PCB& pcb = entry.second;
        for (auto& page : pcb.pageTable) {
            if (page.valid && page.frameIndex == frameIndex) {
                std::cout << "[MemManager] Evicting Page " << page.pageNumber << " from P" << pid << " (Frame " << frameIndex << ")" << std::endl;
                if (page.dirty) {
                    ++pageEvictions;
                }
                page.valid = false;
                page.inMemory = false;
                page.frameIndex = -1;
                frameOccupied[frameIndex] = false;
                return;
            }
        }
    }
}

int MemoryManager::getFreeFrameOrEvict() {
    for (size_t i = 0; i < totalFrames; ++i) {
        if (!frameOccupied[i]) {
            return i;
        }
    }
    if (frameQueue.empty()) {
        return -1;
    }
    int victim = frameQueue.front();
    frameQueue.pop();
    pageOut(victim); 
    return victim;
}

void MemoryManager::showProcessSMI() {
    std::lock_guard<std::mutex> lock(manager_mutex);
    std::cout << "[process-smi]\n";
    for (auto& entry : processTable) {
        int pid = entry.first;
        PCB& pcb = entry.second;
        std::cout << "Process " << pid << " (" << pcb.getName() << "): ";
        for (auto& page : pcb.pageTable) {
            std::cout << page.toString() << " ";
        }
        std::cout << "\n";
    }
}

void MemoryManager::showVMStat() {
    std::lock_guard<std::mutex> lock(manager_mutex);
    size_t used = 0;
    for (bool occ : frameOccupied) used += occ;
    std::cout << "[vmstat]\n";
    std::cout << "Total memory: " << totalMemory << " bytes\n";
    std::cout << "Used memory: " << used * frameSize << " bytes\n";
    std::cout << "Free memory: " << (totalFrames - used) * frameSize << " bytes\n";
    std::cout << "Page faults: " << pageFaults << "\n";
    std::cout << "Page evictions: " << pageEvictions << "\n";
}

// --- FILLED-IN FUNCTION ---
void MemoryManager::snapshotMemory(int quantumCycle) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    if (processTable.empty() && quantumCycle > 0) return;

    std::ostringstream snapshot;
    std::time_t now = std::time(nullptr);
    char timeBuffer[100];
    struct tm localTime;
    #if defined(_WIN32)
        localtime_s(&localTime, &now);
    #else
        localtime_r(&now, &localTime);
    #endif
    std::strftime(timeBuffer, sizeof(timeBuffer), "(%m/%d/%Y %I:%M:%S%p)", &localTime);
    snapshot << "Timestamp: " << timeBuffer << "\n";

    size_t numProcessesActuallyInMemory = 0;
    for(const auto& entry : processTable) {
        for(const auto& page : entry.second.pageTable) {
            if(page.valid) {
                numProcessesActuallyInMemory++;
                break;
            }
        }
    }
    snapshot << "Number of processes in memory: " << numProcessesActuallyInMemory << "\n";

    size_t freeFrames = 0;
    for (bool occupied : frameOccupied) {
        if (!occupied) ++freeFrames;
    }
    size_t externalFragBytes = freeFrames * frameSize;
    snapshot << "Total external fragmentation in KB: " << (externalFragBytes / 1024) << "\n\n";

    snapshot << "----end---- = " << totalMemory << "\n\n";

    struct Block {
        size_t upper;
        std::string name_label;
        size_t lower;
    };
    std::vector<Block> blocks_to_print;
    std::map<int, std::pair<int, int>> process_memory_frame_ranges;

    for (const auto& entry : processTable) {
        int pid = entry.first;
        const PCB& pcb = entry.second;
        int min_frame_idx = -1, max_frame_idx = -1;
        for (const auto& page : pcb.pageTable) {
            if (page.valid && page.frameIndex >= 0) {
                if (min_frame_idx == -1 || page.frameIndex < min_frame_idx) min_frame_idx = page.frameIndex;
                if (max_frame_idx == -1 || page.frameIndex > max_frame_idx) max_frame_idx = page.frameIndex;
            }
        }
        if (min_frame_idx != -1) {
            process_memory_frame_ranges[pid] = {min_frame_idx, max_frame_idx};
        }
    }

    for (const auto& pair : process_memory_frame_ranges) {
        const PCB& pcb = processTable.at(pair.first);
        size_t lower_addr = static_cast<size_t>(pair.second.first) * frameSize;
        size_t upper_addr = static_cast<size_t>(pair.second.second + 1) * frameSize;
        blocks_to_print.push_back({ upper_addr, pcb.getName(), lower_addr });
    }

    std::sort(blocks_to_print.begin(), blocks_to_print.end(), [](const Block& a, const Block& b) {
        return a.upper > b.upper;
    });

    for (const auto& block : blocks_to_print) {
        snapshot << block.upper << "\n";
        snapshot << block.name_label << "\n"; 
        snapshot << block.lower << "\n\n";
    }

    snapshot << "----start---- = 0\n";
    std::string snapshotStr = snapshot.str();
    std::string signature = std::to_string(std::hash<std::string>{}(snapshotStr));

    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        if (signature == last_snapshot_signature && global_quantum_cycle != 0) {
             return; 
        }
        last_snapshot_signature = signature;
        snapshot_log.push_back(snapshotStr);
    }

    std::string folder = "snapshots";
    if (!fs::exists(folder)) {
        fs::create_directory(folder);
    }

    std::string fileName = folder + "/memory_stamp_" + std::to_string(quantumCycle) + ".txt";
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex); 
        background_tasks.push_back(std::async(std::launch::async, [fileName, snapshotStr]() {
            std::ofstream out(fileName);
            if (out.is_open()) {
                out << snapshotStr;
                out.close();
            }
        }));
    }
}

// --- FILLED-IN FUNCTION ---
void MemoryManager::flushAsyncWrites() {
    for (auto& task : background_tasks) {
        if (task.valid()) {
            task.get(); // Wait for each async task to complete
        }
    }
    if (!snapshot_log.empty()) {
        std::cout << "[MemManager] " << snapshot_log.size() << " memory snapshots saved to disk." << std::endl;
    }
    background_tasks.clear();
}