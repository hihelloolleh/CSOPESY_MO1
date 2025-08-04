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
#include <limits>


namespace fs = std::filesystem;

MemoryManager::MemoryManager(const Config& config)
    : totalMemory(config.max_overall_mem),
    frameSize(config.mem_per_frame),
    backing_store_filename("csopesy-backing-store.txt") 
{
    if (fs::exists(backing_store_filename)) {
        fs::remove(backing_store_filename);
        std::cout << "[MemManager] Removed old backing store file." << std::endl;
    }

    totalFrames = totalMemory / frameSize;
    max_pages_per_process = config.max_mem_per_proc / frameSize;


    std::cout << "[MemManager] Initializing with " << totalFrames << " frames of " << frameSize << " bytes each." << std::endl;

    physicalMemory.resize(totalFrames, std::vector<uint8_t>(frameSize, 0));
    frameOccupied.resize(totalFrames, false);
}

MemoryManager::~MemoryManager() {
    flushAsyncWrites();
}

// Writes a page of data to its unique position in the backing store file.
void MemoryManager::writePageToBackingStore(int pid, size_t pageNum, const std::vector<uint8_t>& pageData) {
    std::streampos position = (pid * max_pages_per_process + pageNum) * frameSize;

    std::fstream backingStore(backing_store_filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!backingStore) {
        backingStore.open(backing_store_filename, std::ios::out | std::ios::binary);
        backingStore.close();
        backingStore.open(backing_store_filename, std::ios::in | std::ios::out | std::ios::binary);
    }

    backingStore.seekp(position);
    backingStore.write(reinterpret_cast<const char*>(pageData.data()), frameSize);
}

// Reads a page of data from its unique position in the backing store file.
void MemoryManager::readPageFromBackingStore(int pid, size_t pageNum, std::vector<uint8_t>& pageData) {
    std::streampos position = (pid * max_pages_per_process + pageNum) * frameSize;

    std::ifstream backingStore(backing_store_filename, std::ios::binary);
    if (backingStore) {
        backingStore.seekg(position);
        backingStore.read(reinterpret_cast<char*>(pageData.data()), frameSize);
        // If the read doesn't get all the bytes (e.g., file is new),
        // the remaining bytes in pageData will stay as they were (likely zero).
    }
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
        Page p(static_cast<int>(pid), static_cast<int>(i));
        pcb.addPage(std::move(p));
    }
    
    /*
    std::cout << "[MemManager] Allocated page table for process " << pid << " (" << name << ") requiring " 
              << memoryRequired << " bytes (" << pagesNeeded << " virtual pages)." << std::endl;
    */
    processTable[pid] = std::move(pcb);
    return true;
}

void MemoryManager::removeProcess(int pid) {
    std::lock_guard<std::mutex> lock(manager_mutex);

    auto it = processTable.find(pid);
    if (it == processTable.end()) return;

    PCB& pcb = it->second;
    for (auto& page : pcb.pageTable) {
        if (page.valid && page.frameIndex != Page::INVALID_FRAME) {
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
    size_t frameIndex = getFreeFrameOrEvict();
    if (frameIndex == Page::INVALID_FRAME) {
        std::cerr << "[MemManager] CRITICAL: No frames available. Cannot page in for P" << pcb.getPid() << ".\n";
        return;
    }

    if (page.onBackingStore) {
        // This page was previously paged out, so its data exists on disk.
        readPageFromBackingStore(pcb.getPid(), page.pageNumber, physicalMemory[frameIndex]);
        std::cout << "[MemManager] Paged in P" << pcb.getPid() << " Page " << page.pageNumber << " from backing store.\n";
    }
    else {
        // This is the first time the page is touched. It's new, so zero-fill it.
        std::fill(physicalMemory[frameIndex].begin(), physicalMemory[frameIndex].end(), 0);
    }

    frameOccupied[frameIndex] = true;
    frameToPageMap[frameIndex] = { pcb.getPid(), page.pageNumber };
    page.frameIndex = frameIndex;
    page.valid = true;
    page.inMemory = true;
    page.dirty = false;
    frameQueue.push(frameIndex);
    pageFaults++;
}

void MemoryManager::pageOut(size_t frameIndex) {
    if (frameToPageMap.find(frameIndex) == frameToPageMap.end()) {
        return;
    }

    auto page_id = frameToPageMap.at(frameIndex);
    int pid = page_id.first;
    size_t pageNum = page_id.second;

    auto pcb_it = processTable.find(pid);
    if (pcb_it == processTable.end()) return;

    PCB& pcb = pcb_it->second;
    if (pageNum >= pcb.pageTable.size()) return;

    Page& page = pcb.pageTable[pageNum];

    //If the page is dirty, write its contents to the backing store. >>>
    if (page.dirty) {
        std::cout << "[MemManager] Dirty Page " << page.pageNumber << " of P" << pid
            << " is being written to backing store from Frame " << frameIndex << ".\n";
        writePageToBackingStore(pid, pageNum, physicalMemory[frameIndex]);
        page.onBackingStore = true; // Mark that this page now has a representation on disk.
        pageEvictions++;
    }

    page.valid = false;
    page.inMemory = false;
    page.frameIndex = Page::INVALID_FRAME;
    frameOccupied[frameIndex] = false;
    frameToPageMap.erase(frameIndex);
}

size_t MemoryManager::getFreeFrameOrEvict() {
    for (size_t i = 0; i < totalFrames; ++i) {
        if (!frameOccupied[i]) {
            return i;
        }
    }

    if (frameQueue.empty()) {
        return -1;
    }
    
    size_t victimFrame = Page::INVALID_FRAME;
    while (!frameQueue.empty()) {
        size_t potentialVictim = frameQueue.front();
        frameQueue.pop();
        if (frameOccupied[potentialVictim]) {
            victimFrame = potentialVictim;
            break;
        }
    }
    
    if (victimFrame != Page::INVALID_FRAME) {
        pageOut(victimFrame);
        return victimFrame;
    }

    return Page::INVALID_FRAME;
}

void MemoryManager::snapshotMemory(uint64_t tick) {
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
            size_t pageNum = pageInfo.second;
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
    std::string fileName = folder + "/memory_stamp_" + std::to_string(tick) + ".txt";
    
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