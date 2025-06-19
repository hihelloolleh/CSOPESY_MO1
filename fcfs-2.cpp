#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <atomic>
#include <conio.h>

#define NUM_CORES 4
#define MAX_PROCESSES 10

using namespace std;
std::atomic<bool> running(true);
std::atomic<int> inputRow(40); 

struct Process {
    string name;
    int total_prints;
    int finished_prints = 0;
    string start_time;
    string end_time;
    int assigned_core = -1;
    bool finished = false;
};

mutex queue_mutex;
condition_variable queue_cv;
queue<Process*> task_queue;

bool scheduler_done = false;

constexpr int screenHeight = 50;
constexpr int reservedRows = 2;
constexpr int monitorTopRow = 3;
constexpr int commandPromptRow = screenHeight - reservedRows;

mutex consoleMutex;

string get_timestamp() {
    auto now = chrono::system_clock::now();
    time_t time = chrono::system_clock::to_time_t(now);
    tm* local_tm = localtime(&time);

    stringstream ss;
    ss << put_time(local_tm, "(%m/%d/%Y %I:%M:%S%p)");
    return ss.str();
}

int getConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
    return 80;
}

void drawCommandPrompt(const string& currentInput) {
    lock_guard<mutex> lock(consoleMutex);    

    int width = getConsoleWidth();
    string prompt = "Enter a command: ";
    COORD coord = { 0, static_cast<SHORT>(inputRow.load()) };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);

    cout << string(width, ' ');
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
    cout << prompt << currentInput;
    cout.flush();
}



void printResponseLine(const string& text) {
    lock_guard<mutex> lock(consoleMutex);

    int width = getConsoleWidth();
    int startRow = inputRow.load() + 1;
    int maxLinesToClear = reservedRows - 1;

    for (int i = 0; i < maxLinesToClear; ++i) {
        COORD clearCoord = { 0, static_cast<SHORT>(startRow + i) };
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), clearCoord);
        cout << string(width, ' ');
    }

    int linesNeeded = (text.length() + width - 1) / width;
    linesNeeded = min(linesNeeded, maxLinesToClear);

    for (int i = 0; i < linesNeeded; ++i) {
        string line = text.substr(i * width, width);
        COORD coord = { 0, static_cast<SHORT>(startRow + i) };
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
        cout << line;
    }

    cout.flush();
}


void processCommand(const std::string& input) {
    if (input == "quit") {
        printResponseLine("Quitting...");
        running = false;
    } else if (input == "screen -ls") {
        printResponseLine("Executing screen -ls...");
    } else {
        printResponseLine("Unknown command: '" + input + "'. Try 'screen -ls' or 'quit'.");
    }
}

void inputLoop() {
    string input = "";
    int cursorPos = 0;

    while (running) {
        int width = getConsoleWidth();
        string prompt = "Enter a command: ";
        int inputMaxLength = width - static_cast<int>(prompt.length()) - 1;

        drawCommandPrompt(input);

        if (_kbhit()) {
            char ch = _getch();

            if (ch == '\r') {
                processCommand(input);
                input.clear();
                cursorPos = 0;
            } else if (ch == '\b' && !input.empty()) {
                input.pop_back();
                cursorPos--;
            } else if (isprint(ch) && cursorPos < inputMaxLength) {
                input += ch;
                cursorPos++;
            }
        }
        this_thread::sleep_for(chrono::milliseconds(30));
    }
}

void scheduler_thread(vector<Process*>& processes) {
    for (auto& process : processes) {
        {
            lock_guard<mutex> lock(queue_mutex);
            task_queue.push(process);
        }
        queue_cv.notify_one();
        this_thread::sleep_for(chrono::seconds(1));
    }

    {
        lock_guard<mutex> lock(queue_mutex);
        scheduler_done = true;
    }
    queue_cv.notify_all();
}

void cpu_core_worker(int core_id) {
    while (true) {
        Process* process = nullptr;

        {
            unique_lock<mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !task_queue.empty() || scheduler_done; });

            if (!task_queue.empty()) {
                process = task_queue.front();
                task_queue.pop();
            } else if (scheduler_done) {
                return;
            }
        }

        if (process) {
            process->assigned_core = core_id;
            process->start_time = get_timestamp();

            string filename = process->name + ".txt";

            ifstream check_file(filename);
            bool file_exists = check_file.good();
            check_file.close();

            ofstream log_file(filename, ios::app);

            if (!file_exists) {
                log_file << "Process name: " << process->name << "\n";
                log_file << "Logs:\n\n";
            }

            for (int i = 0; i < process->total_prints; ++i) {
                string timestamp = get_timestamp();
                log_file << timestamp << " Core:" << core_id << " \"Hello world from " << process->name << "!\"" << endl;
                this_thread::sleep_for(chrono::seconds(1));
                process->finished_prints++;
            }

            log_file.close();
            process->end_time = get_timestamp();
            process->finished = true;
        }
    }
}

void monitor_thread(const vector<Process*>& processes) {
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(500));

        {
            lock_guard<mutex> lock(consoleMutex);
            system("cls");

            cout << "\n=========================================" << endl;
            cout << "            Running processes" << endl;
            cout << "=========================================" << endl;

            for (const auto& p : processes) {
                if (p->assigned_core != -1) {
                    cout << p->name << " " << p->start_time
                         << "  Core: " << p->assigned_core << "  ";
                    if (p->finished) {
                        cout << "FINISHED" << endl;
                    } else {
                        cout << p->finished_prints << " / " << p->total_prints << endl;
                    }
                }
            }

            cout << "\n=========================================" << endl;
            cout << "            Finished processes" << endl;
            cout << "=========================================" << endl;

            for (const auto& p : processes) {
                if (p->finished) {
                    cout << p->name << " " << p->end_time
                         << "  Finished " << p->finished_prints << " / " << p->total_prints << endl;
                }
            }

            bool all_done = true;
            for (const auto& p : processes) {
                if (!p->finished) {
                    all_done = false;
                    break;
                }
            }

            int printedLines = 6; 
            for (const auto& p : processes) {
                if (p->assigned_core != -1)
                    printedLines++;
            }
            printedLines += 3; 
            for (const auto& p : processes) {
                if (p->finished)
                    printedLines++;
            }

            inputRow.store(printedLines + 1);
        } 

        if (scheduler_done) {
            bool all_done = true;
            for (const auto& p : processes) {
                if (!p->finished) {
                    all_done = false;
                    break;
                }
            }
            if (all_done) break;
        }
    }
}


int main() {
    srand(time(nullptr));

    vector<Process*> processes;
    int process_count = 10;

    for (int i = 0; i < process_count; ++i) {
        Process* p = new Process;
        p->name = "process" + to_string(i + 1);
        p->total_prints = rand() % 5 + 3;
        processes.push_back(p);
    }

    thread scheduler(scheduler_thread, ref(processes));

    vector<thread> workers;
    for (int i = 0; i < NUM_CORES; ++i) {
        workers.emplace_back(cpu_core_worker, i);
    }

    thread monitor(monitor_thread, cref(processes));
    thread input(inputLoop);

    scheduler.join();
    for (auto& t : workers) t.join();
    monitor.join();
    input.join();

    for (auto p : processes) delete p;

    return 0;
}

