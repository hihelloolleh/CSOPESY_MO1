#include "mem_manager.h"
#include "pcb.h"
#include "page.h"
#include "process.h"
#include "config.h"
#include "shared_globals.h" // <--- THIS IS THE MISSING INCLUDE!
#include <iostream>
#include <iomanip>
#include <cstring>
#include <sstream>    
#include <fstream>    
#include <ctime>      
#include <chrono>
#include <filesystem>
#include <algorithm> // For std::min, std::max, std::sort
#include <map>       // For std::map to store process memory ranges

namespace fs = std::filesystem;

MemoryManager::MemoryManager(const Config& config)
    : totalMemory(config.max_overall_mem),
    frameSize(config.mem_per_frame) {

    totalFrames = static_cast<int>(totalMemory / frameSize);

    physicalMemory.resize(totalFrames, std::vector<uint8_t>(frameSize, 0));
    frameOccupied.resize(totalFrames, false);
    backingStore.open(backingStoreFile, std::ios::in | std::ios::out | std::ios::trunc);
}


bool MemoryManager::createProcess(const Process& proc, size_t memoryRequired) {
    int pid = proc.id;
    const std::string& name = proc.name;

    if (processTable.find(pid) != processTable.end()) return false;

    size_t pagesNeeded = (memoryRequired + frameSize - 1) / frameSize;

    // Check available frames
    size_t freeFrames = 0;
    for (bool used : frameOccupied)
        if (!used) ++freeFrames;

    if (freeFrames < pagesNeeded) {
        std::cerr << "[mem-manager] Not enough memory to allocate process " << name << ". Free frames: " << freeFrames << ", Needed: " << pagesNeeded << "\n";
        return false; // Reject process for now
    }

    PCB pcb(pid, name);

    // Add instruction pages
    for (int pageNum : proc.insPages) {
        Page p(pid, pageNum);
        p.entryNumber = pageNum;
        p.inMemory = false;
        pcb.addPage(std::move(p));
    }

    // Add variable pages
    for (int pageNum : proc.varPages) {
        Page p(pid, pageNum);
        p.entryNumber = pageNum;
        p.inMemory = false;
        pcb.addPage(std::move(p));
    }

    processTable[pid] = std::move(pcb);
    return true;
}

void MemoryManager::removeProcess(int pid) {
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

    // Ensure offset + sizeof(uint16_t) does not exceed frameSize
    if (offset + sizeof(uint16_t) > frameSize) {
        std::cerr << "[mem-manager] Read error: Address " << address << " (offset " << offset << ") exceeds frame boundary for pid " << pid << ".\n";
        return false; 
    }

    value = *reinterpret_cast<uint16_t*>(&physicalMemory[page.frameIndex][offset]);
    return true;
}

bool MemoryManager::writeMemory(int pid, uint16_t address, uint16_t value) {
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
    
    // Ensure offset + sizeof(uint16_t) does not exceed frameSize
    if (offset + sizeof(uint16_t) > frameSize) {
        std::cerr << "[mem-manager] Write error: Address " << address << " (offset " << offset << ") exceeds frame boundary for pid " << pid << ".\n";
        return false;
    }

    std::memcpy(&physicalMemory[page.frameIndex][offset], &value, sizeof(uint16_t));
    page.dirty = true;
    return true;
}

Process* MemoryManager::getProcess(int pid) {
    auto it = processTable.find(pid);
    if (it == processTable.end()) return nullptr;
    return &it->second.process;
}

void MemoryManager::pageIn(PCB& pcb, Page& page) {
    int frameIndex = getFreeFrameOrEvict();
    // After getFreeFrameOrEvict, frameOccupied[frameIndex] might already be false
    // if a page was evicted from there. Ensure it's true for the *new* page.
    frameOccupied[frameIndex] = true; 
    
    // Update LRU queue for eviction logic
    frameQueue.push(frameIndex); 

    // Simulate page load with 0s
    std::fill(physicalMemory[frameIndex].begin(), physicalMemory[frameIndex].end(), 0);

    page.frameIndex = frameIndex;
    page.valid = true;
    page.inMemory = true; // Mark as in memory
    pageFaults++;
}

void MemoryManager::pageOut(int frameIndex) {
    // Iterate through process table to find which page occupies this frame
    for (auto& entry : processTable) {
        int pid = entry.first;
        PCB& pcb = entry.second;

        for (auto& page : pcb.pageTable) {
            if (page.valid && page.frameIndex == frameIndex) {
                if (page.dirty) {
                    // Simulate writing dirty page back to backing store
                    // backingStore << "Evict P" << pid << " Page " << page.pageNumber << " from frame " << frameIndex << "\n";
                    ++pageEvictions;
                }
                page.valid = false;
                page.inMemory = false; // Mark as not in memory
                page.frameIndex = -1;
                frameOccupied[frameIndex] = false; // Mark frame as free
                return; // Found and handled the page, can exit
            }
        }
    }
}

int MemoryManager::getFreeFrameOrEvict() {
    // First, try to find a truly free frame
    for (int i = 0; i < totalFrames; ++i) {
        if (!frameOccupied[i]) {
            return i;
        }
    }

    // If no free frames, evict the oldest page (FIFO approximation of LRU)
    if (frameQueue.empty()) {
        std::cerr << "[mem-manager] Error: Frame queue is empty but no free frames found! This indicates a logic error.\n";
        // As a fallback, try to find any occupied frame to evict, but this shouldn't be reached
        for (int i = 0; i < totalFrames; ++i) {
            if (frameOccupied[i]) {
                pageOut(i);
                return i;
            }
        }
        return -1; // Critical error
    }

    int victim = frameQueue.front();
    frameQueue.pop();
    pageOut(victim); 
    // After pageOut, frameOccupied[victim] will be set to false.
    return victim;
}

void MemoryManager::showProcessSMI() {
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
    size_t used = 0;
    for (bool occ : frameOccupied) used += occ;

    std::cout << "[vmstat]\n";
    std::cout << "Total memory: " << totalMemory << " bytes\n";
    std::cout << "Used memory: " << used * frameSize << " bytes\n";
    std::cout << "Free memory: " << (totalFrames - used) * frameSize << " bytes\n";
    std::cout << "Page faults: " << pageFaults << "\n";
    std::cout << "Page evictions: " << pageEvictions << "\n";
}

void MemoryManager::snapshotMemory(int quantumCycle) {
    // Return early if no processes have ever been in memory and it's not the initial state (quantum 0)
    // This prevents empty snapshots unless explicitly starting with no processes at Q0.
    if (processTable.empty() && quantumCycle > 0) return; 

    std::ostringstream snapshot;

    // Get timestamp (formatted)
    std::time_t now = std::time(nullptr);
    char timeBuffer[100];
    struct tm localTime;
    #if defined(_WIN32)
        localtime_s(&localTime, &now); // Windows specific
    #else
        localtime_r(&now, &localTime); // POSIX (Linux/macOS) specific
    #endif
    std::strftime(timeBuffer, sizeof(timeBuffer), "(%m/%d/%Y %I:%M:%S%p)", &localTime);
    snapshot << "Timestamp: " << timeBuffer << "\n";

    // Count processes currently in memory
    size_t numProcessesActuallyInMemory = 0;
    for(const auto& entry : processTable) {
        const PCB& pcb = entry.second;
        for(const auto& page : pcb.pageTable) {
            if(page.valid) { // If at least one page is valid (in physical memory), count the process.
                numProcessesActuallyInMemory++;
                break; // Move to the next process, no need to check other pages of this one.
            }
        }
    }
    snapshot << "Number of processes in memory: " << numProcessesActuallyInMemory << "\n";

    // External Fragmentation
    size_t freeFrames = 0;
    for (bool occupied : frameOccupied) {
        if (!occupied) ++freeFrames;
    }
    size_t externalFragBytes = freeFrames * frameSize;
    snapshot << "Total external fragmentation in KB: " << (externalFragBytes / 1024) << "\n\n";

    // List of memory blocks from high to low
    snapshot << "----end---- = " << totalMemory << "\n\n";

    struct Block {
        size_t upper;
        std::string pid_label; // Changed from 'pid' to avoid confusion with actual int pid
        size_t lower;
    };
    std::vector<Block> blocks_to_print;

    // Use a map to store the minimum and maximum physical frame indices
    // for each process that currently has pages in memory.
    std::map<int, std::pair<int, int>> process_memory_frame_ranges; // pid -> {min_frame_idx, max_frame_idx}

    // Iterate through all processes in the processTable to find their memory ranges
    for (const auto& entry : processTable) {
        int pid = entry.first;
        const PCB& pcb = entry.second;
        
        int min_frame_idx = -1; // Initialize to an invalid index
        int max_frame_idx = -1; // Initialize to an invalid index

        // Iterate through all pages belonging to the current process
        for (const auto& page : pcb.pageTable) {
            // Check if the page is valid (in memory) and has a valid frame index
            if (page.valid && page.frameIndex >= 0 && static_cast<size_t>(page.frameIndex) < frameOccupied.size()) {
                // Update the minimum and maximum frame indices for this process
                if (min_frame_idx == -1 || page.frameIndex < min_frame_idx) {
                    min_frame_idx = page.frameIndex;
                }
                if (max_frame_idx == -1 || page.frameIndex > max_frame_idx) {
                    max_frame_idx = page.frameIndex;
                }
            }
        }
        
        // If the process has at least one page currently in physical memory,
        // store its determined frame range.
        if (min_frame_idx != -1) { 
            process_memory_frame_ranges[pid] = {min_frame_idx, max_frame_idx};
        }
    }

    // Convert the collected process frame ranges into byte addresses for printing.
    for (const auto& pair : process_memory_frame_ranges) {
        int pid = pair.first;
        int min_frame_idx = pair.second.first;
        int max_frame_idx = pair.second.second;

        // Calculate the lower address (start of the lowest frame occupied)
        size_t lower_addr = static_cast<size_t>(min_frame_idx) * frameSize;
        // Calculate the upper address (end of the highest frame occupied)
        size_t upper_addr = static_cast<size_t>(max_frame_idx + 1) * frameSize;

        blocks_to_print.push_back({ upper_addr, "P" + std::to_string(pid), lower_addr });
    }

    // Sort blocks in descending order by their upper address, as requested by the mockup.
    std::sort(blocks_to_print.begin(), blocks_to_print.end(), [](const Block& a, const Block& b) {
        return a.upper > b.upper;
    });

    // Print the sorted memory blocks
    for (const auto& block : blocks_to_print) {
        snapshot << block.upper << "\n";
        snapshot << block.pid_label << "\n"; // Use pid_label
        snapshot << block.lower << "\n\n";
    }

    snapshot << "----start---- = 0\n";

    std::string snapshotStr = snapshot.str();

    // Compute signature to avoid writing identical snapshots.
    std::string signature = std::to_string(std::hash<std::string>{}(snapshotStr));

    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        // Only skip if the signature is identical AND it's not the very first snapshot (quantum 0)
        if (signature == last_snapshot_signature && global_quantum_cycle != 0) {
             return; 
        }
        last_snapshot_signature = signature;
        snapshot_log.push_back(snapshotStr);
    }

    // Ensure 'snapshots' directory exists, and asynchronously write to file.
    std::string folder = "snapshots";
    if (!fs::exists(folder)) {
        std::error_code ec; // For error checking directory creation
        if (!fs::create_directory(folder, ec)) {
            std::cerr << "[MemoryManager] Error: Could not create snapshots directory: " << ec.message() << std::endl;
            return;
        }
    }

    std::string fileName = folder + "/memory_stamp_" + std::to_string(quantumCycle) + ".txt";
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex); 
        background_tasks.push_back(std::async(std::launch::async, [fileName, snapshotStr]() {
            std::ofstream out(fileName);
            if (out.is_open()) {
                out << snapshotStr;
                out.close();
            } else {
                std::cerr << "[MemoryManager] Error: Could not open snapshot file " << fileName << " for writing.\n";
            }
        }));
    }
}

void MemoryManager::flushAsyncWrites() {
    for (auto& task : background_tasks) {
        if (task.valid()) task.get(); // Wait for each async task to complete
    }

    std::cout << "[mem-manager] " << snapshot_log.size() << " snapshots saved to disk.\n";
    background_tasks.clear();
}