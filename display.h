#ifndef DISPLAY_H
#define DISPLAY_H

#include <iostream>
#include "process.h"

void clear_console();
void print_header();
void generate_system_report(std::ostream& output_stream);

void display_process_view(Process* process);

void show_global_process_smi();
void show_vmstat();

#endif // DISPLAY_H