#pragma once
#include <string>
#include <vector>
#include "process.h"
#include "page.h"

class PCB {
public:
    Process process;
    std::vector<Page> pageTable;
    bool isActive = false;

    // default
    PCB() : process(0, "") {}

    // parameterized
    PCB(int id, const std::string& n) : process(id, n) {}

    void addPage(Page&& p) {
        pageTable.push_back(std::move(p));
    }

    std::string getName() const {
        return process.name;
    }

    int getPid() const {
        return process.id;
    }
};

