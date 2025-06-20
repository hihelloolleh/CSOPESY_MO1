#ifndef DISPLAY_H
#define DISPLAY_H

#include <iostream>
#include "process.h"

void clear_console();
void print_header(); //WAIT LANG DITO
void generate_system_report(std::ostream& output_stream);

void display_process_view(Process* process);

#endif // DISPLAY_H