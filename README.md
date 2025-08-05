## Members: 
ANG, GERMAINE (S13)<br>
CABREROS, SAMANTHA JADE (S15)<br>
TOGADO, DALRIANNE FRANCESCA (S14)<br>
WANGKAY, JEANTE (S13)<br>

## How To Run: 
1. Type this command into the terminal to build the program. <br>
   **windows:** `g++ -std=c++17 config.cpp cpu_core.cpp display.cpp instructions.cpp main.cpp scheduler_utils.cpp scheduler.cpp shared_globals.cpp -o csopesy_emu.exe` <br>
   **mac:** `g++ -std=c++17 -pthread -o csopesy_emu *.cpp`
3. Afterwards, type `csopesy_emu.exe` to run the program.
4. Type `initialize` to initialize the program.
5. You may now input the other commands accordingly. 

## Architecture Overview
The simulator is built on a few key components that work together:

**main.cpp:** The entry point. It initializes the system, starts the main threads (CLI, process generator, clock), and handles shutdown.<br>

**cli_loop():** The main loop that reads and parses user commands from the console.<br>

**cpu_core.cpp:** Defines the cpu_core_worker function, which is the main loop for each CPU thread. It fetches a process from the ready queue and executes its instructions.<br>

**scheduler.cpp:** Contains the process_generator_thread which automatically creates new processes at a configured frequency, and the clock_thread which increments the global system tick.<br>

**scheduler_utils.cpp:** Implements the core scheduling logic, such as select_process() which picks the next process from the queue based on the active scheduling algorithm.<br>

**instructions.cpp:** Contains the implementation for each "Barebones" instruction (PRINT, ADD, FOR, etc.). It acts as the interpreter for the process code.<br>

**config.cpp:** Handles loading and validating settings from the config.txt file.<br>

**display.cpp:** Provides functions for printing formatted output to the console, like system reports and process views.<br>

**-a shared_globals.h:** Declares global variables, mutexes, and condition variables that are shared across all threads to maintain a consistent system state.<br>

## Memory Management Subsystem:
The memory manager is a core component with its own set of classes:

**mem_manager.cpp:** The heart of the memory system. It manages physical frames, implements the page replacement algorithm (FIFO), handles page-in and page-out requests, and tracks memory usage statistics.

**pcb.h (Process Control Block):** A data structure held by the MemoryManager that contains the metadata for a process's memory, including its page table.

**page.h:**  Represents a single entry in a page table, tracking whether the page is valid (in memory), dirty (modified), and where it is located.

## Key Features
**Multi-threading CPU Simulation:** Simulates a multi-core environment where each core runs as a separate thread.<br>

**Pluggable Scheduling Algorithms:** Supports multiple scheduling algorithms (like FCFS and RR) configurable via a text file.<br>

**Demand Paging Memory Management:** Implements a memory manager that only loads pages from a backing store into physical memory when they are needed, handling page faults.<br>

**Backing Store Simulation:** Uses a text file (csopesy-backing-store.txt) to simulate secondary storage for pages that are not in physical memory.<br>

**Memory Protection:** Simulates segmentation faults by terminating processes that attempt to access memory outside their allocated virtual address space.<br>

**Interactive Command-Line Interface:** Allows users to create processes, inject custom instructions, and monitor the system state in real-time.<br>

**System Monitoring Tools:** Includes process-smi and vmstat to provide detailed reports on memory usage, CPU utilization, and paging statistics.<br>

## Commands:
**initialize**	Loads config.txt and starts the CPU core threads. Must be run first.<br>

**scheduler-start**	Starts the automatic process generator.<br>

**scheduler-stop**	Stops the automatic process generator.<br>

**screen -s <name> <size>**	Creates a new process with a given name and memory size with random instructions.<br>

**screen -c <name> <size> "<instr>"**	Creates a new process with a specific set of semi-colon separated instructions.<br>

**screen -r <name>**	Views the state of a finished, or crashed process.<br>

**screen -ls**	Lists all running and finished processes in the system.<br>

**process-smi**	Displays a high-level summary of memory and CPU usage.<br>

**vmstat**	Shows detailed virtual memory statistics, including page-ins and page-outs.<br>

**exit**	Shuts down the simulator and cleans up resources.<br>

