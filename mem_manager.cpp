#include "mem_manager.h"
#include "pcb.h"
#include "page.h"
#include "process.h"
#include "config.h"
#include <iostream>
#include <iomanip>
#include <cstring>


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
        std::cerr << "[mem-manager] Not enough memory to allocate process " << name << "\n";
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
    frameOccupied[frameIndex] = true;
    frameQueue.push(frameIndex);

    // Simulate page load with 0s
    std::fill(physicalMemory[frameIndex].begin(), physicalMemory[frameIndex].end(), 0);

    page.frameIndex = frameIndex;
    page.valid = true;
    ++pageFaults;
}

void MemoryManager::pageOut(int frameIndex) {
    for (auto& entry : processTable) {
        int pid = entry.first;
        PCB& pcb = entry.second;

        for (auto& page : pcb.pageTable) {
            if (page.valid && page.frameIndex == frameIndex) {
                if (page.dirty) {
                    backingStore << "Evict P" << pid << " Page " << page.pageNumber << "\n";
                    ++pageEvictions;
                }
                page.valid = false;
                page.frameIndex = -1;
                return;
            }
        }
    }

}

int MemoryManager::getFreeFrameOrEvict() {
    for (int i = 0; i < totalFrames; ++i) {
        if (!frameOccupied[i]) return i;
    }

    int victim = frameQueue.front();
    frameQueue.pop();
    pageOut(victim);
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
