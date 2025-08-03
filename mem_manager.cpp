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
}

MemoryManager::~MemoryManager() {
    flushAsyncWrites();
}

bool MemoryManager::createProcess(const Process& proc) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    int pid = proc.id;
    const std::string& name = proc.name;
    size_t memoryRequired = proc.memory_required;

    if (processTable.find(pid) != processTable.end()) {
        std::cerr << "[MemManager] Error: Process with PID " << pid << " already exists.\n";
        return false;
    }

    size_t pagesNeeded = (memoryRequired + frameSize - 1) / frameSize;

    PCB pcb(pid, name, memoryRequired);
    
    for (size_t i = 0; i < pagesNeeded; ++i) {
        Page p(pid, i);
        pcb.addPage(std::move(p));
    }
    
    std::cout << "[MemManager] Allocated page table for process " << pid << " (" << name << ") requiring " 
              << memoryRequired << " bytes (" << pagesNeeded << " virtual pages)." << std::endl;

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
        }
    }
    processTable.erase(it);
    std::cout << "[MemManager] Removed process " << pid << " and freed its frames." << std::endl;
}

bool MemoryManager::readMemory(int pid, uint16_t address, uint16_t& value) {
    std::lock_guard<std::mutex> lock(manager_mutex);
    auto it = processTable.find(pid);
    if (it == processTable.end()) return false;

    PCB& pcb = it->second;
    if (address + sizeof(uint16_t) > pcb.getMemoryRequirement()) return false;

    size_t pageNum = address / frameSize;
    size_t offset = address % frameSize;

    if (pageNum >= pcb.pageTable.size()) return false;

    Page& page = pcb.pageTable[pageNum];
    if (!page.valid) {
        pageIn(pcb, page);
        if(!page.valid) return false;
    }

    if (offset + sizeof(uint16_t) > frameSize) {
        std::cerr << "[MemManager] Error: Read for P" << pid << " at " << address << " crosses a page boundary.\n";
        return false;
    }

    std::memcpy(&value, &physicalMemory[page.frameIndex][offset], sizeof(uint16_t));
    page.lastAccessed = cpu_ticks.load();
    return true;
}

bool MemoryManager::writeMemory(int pid, uint16_t address, uint16_t value) {
    std::lock_guard<std::mutex> lock(manager_mutex);
    auto it = processTable.find(pid);
    if (it == processTable.end()) return false;

    PCB& pcb = it->second;
    if (address + sizeof(uint16_t) > pcb.getMemoryRequirement()) return false;

    size_t pageNum = address / frameSize;
    size_t offset = address % frameSize;

    if (pageNum >= pcb.pageTable.size()) return false;

    Page& page = pcb.pageTable[pageNum];
    if (!page.valid) {
        pageIn(pcb, page);
        if(!page.valid) return false;
    }

    if (offset + sizeof(uint16_t) > frameSize) {
         std::cerr << "[MemManager] Error: Write for P" << pid << " at " << address << " crosses a page boundary.\n";
        return false;
    }

    std::memcpy(&physicalMemory[page.frameIndex][offset], &value, sizeof(uint16_t));
    page.dirty = true;
    page.lastAccessed = cpu_ticks.load();
    return true;
}

// --- PHASE 3: NEW METHOD IMPLEMENTATION ---
bool MemoryManager::touchPage(int pid, uint16_t address) {
    std::lock_guard<std::mutex> lock(manager_mutex);
    auto it = processTable.find(pid);
    if (it == processTable.end()) {
        return false; // Process doesn't exist
    }

    PCB& pcb = it->second;
    if (address >= pcb.getMemoryRequirement()) {
        return false; // Address out of bounds for this process
    }

    size_t pageNum = address / frameSize;
    if (pageNum >= pcb.pageTable.size()) {
        return false; // Should be caught by above check, but for safety
    }

    Page& page = pcb.pageTable[pageNum];
    if (!page.valid) {
        // The page is not in a physical frame. This is a page fault.
        pageIn(pcb, page);
        return true; // Return true to signal that a fault occurred.
    }
    
    // The page was already in memory, no fault occurred.
    return false;
}

void MemoryManager::pageIn(PCB& pcb, Page& page) {
    int frameIndex = getFreeFrameOrEvict();
    if (frameIndex == -1) {
        std::cerr << "[MemManager] CRITICAL: No frames available and eviction failed. Cannot page in for P" << pcb.getPid() << ".\n";
        return;
    }
    
    std::fill(physicalMemory[frameIndex].begin(), physicalMemory[frameIndex].end(), 0);

    frameOccupied[frameIndex] = true;
    frameToPageMap[frameIndex] = {pcb.getPid(), page.pageNumber};
    
    page.frameIndex = frameIndex;
    page.valid = true;
    page.inMemory = true;
    page.dirty = false;

    frameQueue.push(frameIndex);

    pageFaults++;
    std::cout << "[MemManager] Page fault for P" << pcb.getPid() << " Page " << page.pageNumber << ". Loaded into Frame " << frameIndex << ".\n";
}

void MemoryManager::pageOut(int frameIndex) {
    if(frameToPageMap.find(frameIndex) == frameToPageMap.end()) {
        return;
    }

    auto page_id = frameToPageMap[frameIndex];
    int pid = page_id.first;
    int pageNum = page_id.second;

    auto pcb_it = processTable.find(pid);
    if (pcb_it == processTable.end()) return;

    PCB& pcb = pcb_it->second;
    if (static_cast<size_t>(pageNum) >= pcb.pageTable.size()) return;

    Page& page = pcb.pageTable[pageNum];

    std::cout << "[MemManager] Evicting Page " << page.pageNumber << " of P" << pid << " from Frame " << frameIndex << ".\n";
    
    if (page.dirty) {
        pageEvictions++;
    }

    page.valid = false;
    page.inMemory = false;
    page.frameIndex = -1;

    frameOccupied[frameIndex] = false;
    frameToPageMap.erase(frameIndex);
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
    
    int victimFrame = -1;
    while(!frameQueue.empty()){
        int potentialVictim = frameQueue.front();
        frameQueue.pop();
        if(frameOccupied[potentialVictim]){
            victimFrame = potentialVictim;
            break;
        }
    }
    
    if (victimFrame != -1) {
        pageOut(victimFrame);
        return victimFrame;
    }

    return -1;
}

void MemoryManager::snapshotMemory(int tick) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    std::ostringstream snapshot;
    snapshot << "--- Memory Snapshot at Tick: " << tick << " ---\n\n";

    size_t usedFrames = 0;
    for (bool occupied : frameOccupied) {
        if (occupied) usedFrames++;
    }
    snapshot << "Physical Memory: " << (usedFrames * frameSize) / 1024 << "KB Used, "
             << ((totalFrames - usedFrames) * frameSize) / 1024 << "KB Free ("
             << usedFrames << "/" << totalFrames << " frames)\n";
    snapshot << "Page Faults: " << pageFaults << " | Dirty Evictions: " << pageEvictions << "\n\n";

    snapshot << "Memory Layout (Address = Frame * " << frameSize << "):\n";
    snapshot << std::setw(10) << "Address" << std::setw(10) << "Frame #" << std::setw(15) << "Content\n";
    snapshot << "------------------------------------------\n";

    for (size_t i = 0; i < totalFrames; ++i) {
        size_t addr = i * frameSize;
        snapshot << std::left << std::setw(10) << addr;
        snapshot << std::left << std::setw(10) << i;
        if (frameOccupied[i] && frameToPageMap.count(i)) {
            auto pageInfo = frameToPageMap.at(i);
            int pid = pageInfo.first;
            int pageNum = pageInfo.second;
            std::string procName = processTable.count(pid) ? processTable.at(pid).getName() : "???";
            snapshot << "P" << pid << " (" << procName << "), Page " << pageNum;
        } else {
            snapshot << "[Free]";
        }
        snapshot << "\n";
    }

    snapshot << "\n--- Process Page Tables ---\n";
    for(const auto& entry : processTable) {
        const PCB& pcb = entry.second;
        snapshot << "PID: " << pcb.getPid() << " (" << pcb.getName() << ") - Requires: " << pcb.getMemoryRequirement() << " bytes\n";
        for(const auto& page : pcb.pageTable) {
            snapshot << "  - Virt Page " << page.pageNumber;
            if (page.valid) {
                 snapshot << " -> Phys Frame " << page.frameIndex << (page.dirty ? " [Dirty]" : " [Clean]");
            } else {
                snapshot << " -> On Disk";
            }
            snapshot << "\n";
        }
        snapshot << "\n";
    }

    std::string snapshotStr = snapshot.str();

    {
        std::lock_guard<std::mutex> sig_lock(snapshot_mutex);
        std::string signature = std::to_string(std::hash<std::string>{}(snapshotStr));
        if (signature == last_snapshot_signature && tick > 0) {
             return; 
        }
        last_snapshot_signature = signature;
    }

    std::string folder = "snapshots";
    if (!fs::exists(folder)) {
        fs::create_directory(folder);
    }
    std::string fileName = folder + "/memory_tick_" + std::to_string(tick) + ".txt";
    
    background_tasks.push_back(std::async(std::launch::async, [fileName, snapshotStr]() {
        std::ofstream out(fileName);
        if (out.is_open()) {
            out << snapshotStr;
        }
    }));
}

void MemoryManager::flushAsyncWrites() {
    std::cout << "[MemManager] Flushing " << background_tasks.size() << " pending snapshot writes to disk..." << std::endl;
    for (auto& task : background_tasks) {
        if (task.valid()) {
            task.get();
        }
    }
    background_tasks.clear();
    std::cout << "[MemManager] All snapshots saved." << std::endl;
}

bool MemoryManager::isProcessActive(int pid) {
    std::lock_guard<std::mutex> lock(manager_mutex);
    return processTable.count(pid) > 0;
}

std::tuple<size_t, size_t> MemoryManager::getMemoryUsageStats() {
    std::lock_guard<std::mutex> lock(manager_mutex);

    size_t usedFrames = 0;
    for (bool occupied : frameOccupied) {
        if (occupied) ++usedFrames;
    }

    size_t usedBytes = usedFrames * frameSize;
    return std::make_tuple(usedBytes, totalMemory);
}