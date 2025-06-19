#include <iostream>
#include <string>
#include <vector>
#include <cstdlib> 
 // test 

void printHeader() {
    std::cout << R"(
-------------------------------------------------                                                                        
  _____  _____  ___________ _____ _______   __
/  __ \/  ___||  _  | ___ \  ___/  ___\ \ / /
| /  \/\ `--. | | | | |_/ / |__ \ `--. \ V / 
| |     `--. \| | | |  __/|  __| `--. \ \ /  
| \__/\/\__/ /\ \_/ / |   | |___/\__/ / | |  
 \____/\____/  \___/\_|   \____/\____/  \_/ 

-------------------------------------------------     
    )" << std::endl;
    std::cout << "Enter command:\n";
}

void clearScreen() {
    #ifdef _WIN32
        system("cls");  // Windows
    #else
        system("clear"); // Unix/Linux/macOS
    #endif
    printHeader();
}

int main() {
    std::vector<std::string> validCommands = {
        "initialize", "screen -s", "screen -r" "scheduler-start",
        "scheduler-stop", "report-util", "process-smi", "clear", "exit"
    };

    bool running = true;
    printHeader();

    while (running) {
        std::string input;
        std::cout << "> ";
        std::getline(std::cin, input);

        if (input == "exit") {
            std::cout << "Exiting application.\n";
            running = false;
        }
        else if (input == "clear") {
            clearScreen();
        }
        else {
            bool found = false;
            for (const auto& cmd : validCommands) {
                if (input == cmd) {
                    std::cout << cmd << " command recognized. Doing something.\n";
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cout << "Unknown command: " << input << "\n";
            }
        }
    }

    return EXIT_SUCCESS;
}