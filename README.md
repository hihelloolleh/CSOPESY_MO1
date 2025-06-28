## Members: 
ANG, GERMAINE (S13)<br>
CABREROS, SAMANTHA JADE (S15)<br>
TOGADO, DALRIANNE FRANCESCA (S14)<br>
WANGKAY, JEANTE (S13)<br>

## How To Run: 
1. Type this command into the terminal to build the program. <br>
   **windows:** `g++ -std=c++17 config.cpp cpu_core.cpp display.cpp instructions.cpp main.cpp scheduler_utils.cpp scheduler.cpp shared_globals.cpp -o csopesy_emu.exe` <br>
   **mac:** `g++ -std=c++17 -pthread -o csopesy_emu *.cpp`
3. Afterwards, type `csopesy_emu_exe` to run the program.
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

-a shared_globals.h: Declares global variables, mutexes, and condition variables that are shared across all threads to maintain a consistent system state.<br>
