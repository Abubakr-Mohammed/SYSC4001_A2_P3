#ifndef INTERRUPTS_HPP_
#define INTERRUPTS_HPP_

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <random>
#include <utility>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdio.h>

// basic constants used by interrupt vector address calculation
#define ADDR_BASE   0
#define VECTOR_SIZE 2

// Partition structure
struct Partition {
    unsigned int partition_number;
    unsigned int size;
    std::string code; // "empty", "init" or program name
    Partition() : partition_number(0), size(0), code("empty") {}
    Partition(unsigned int pnum, unsigned int s, std::string c) : partition_number(pnum), size(s), code(c) {}
};

// global memory map — 6 fixed partitions as described in the assignment
static Partition memory[6] = {
    Partition(0,40,"empty"),
    Partition(1,25,"empty"),
    Partition(2,15,"empty"),
    Partition(3,10,"empty"),
    Partition(4,8,"empty"),
    Partition(5,2,"empty")
};

// PCB struct (simple)
struct PCB {
    unsigned int PID;
    int PPID;
    std::string program_name;
    unsigned int size; // size in MB
    int partition_number; // -1 if not allocated

    PCB(): PID(0), PPID(-1), program_name("init"), size(1), partition_number(-1) {}
    PCB(unsigned int _pid, int _ppid, std::string _pn, unsigned int _size, int _part_num)
        : PID(_pid), PPID(_ppid), program_name(_pn), size(_size), partition_number(_part_num) {}
};

// Representation of external files (persistent)
struct external_file{
    std::string program_name;
    unsigned int size;
};

// Allocates memory partition for the PCB; choose smallest partition that fits
bool allocate_memory(PCB* current) {
    for (int i = 5; i >= 0; --i) { // start from smallest partition (index 5 is 2MB)
        if (memory[i].size >= current->size && memory[i].code == "empty") {
            current->partition_number = memory[i].partition_number;
            memory[i].code = current->program_name;
            return true;
        }
    }
    return false;
}

// Frees the memory partition occupied by process (if any)
void free_memory(PCB* process) {
    if (process->partition_number >= 0 && process->partition_number < 6) {
        memory[process->partition_number].code = "empty";
        process->partition_number = -1;
    }
}

// split helper
std::vector<std::string> split_delim(std::string input, std::string delim) {
    std::vector<std::string> tokens;
    size_t pos;
    while ((pos = input.find(delim)) != std::string::npos) {
        tokens.push_back(input.substr(0,pos));
        input.erase(0,pos+delim.length());
    }
    tokens.push_back(input);
    return tokens;
}

// parse trace line -> tuple: activity, numeric field, optional program name
std::tuple<std::string, int, std::string> parse_trace(std::string trace) {
    auto parts = split_delim(trace, ",");
    if (parts.size() < 2) {
        std::cerr << "Error: Malformed input line: " << trace << std::endl;
        return {"null",-1,"null"};
    }
    std::string activity = parts[0];
    // trim spaces
    while (!activity.empty() && activity.front()==' ') activity.erase(0,1);
    while (!activity.empty() && activity.back()==' ') activity.pop_back();

    int duration_intr = 0;
    try {
        duration_intr = std::stoi(parts[1]);
    } catch(...) {
        duration_intr = 0;
    }

    std::string extern_file = "null";
    // if activity starts with "EXEC <name>" split by ' '
    auto sp = split_delim(activity, " ");
    if (!sp.empty() && sp[0] == "EXEC" && sp.size() >= 2) {
        extern_file = sp[1];
        activity = "EXEC";
    }
    // also normalize IF_CHILD, IF_PARENT, ENDIF tokens
    if (activity == "IF_CHILD" || activity == "IF_PARENT" || activity == "ENDIF") {
        // keep as-is
    }
    if (activity == "ENDIO") activity = "ENDIO"; // normalize (already)
    return {activity, duration_intr, extern_file};
}

// intr boilerplate returns pair: (execution log fragment, updated_time)
std::pair<std::string,int> intr_boilerplate(int current_time, int intr_num, int context_save_time, std::vector<std::string> vectors) {
    std::string execution = "";
    execution += std::to_string(current_time) + ", " + std::to_string(1) + ", switch to kernel mode\n";
    current_time += 1;
    execution += std::to_string(current_time) + ", " + std::to_string(context_save_time) + ", context saved\n";
    current_time += context_save_time;
    char vector_address_c[10];
    sprintf(vector_address_c, "0x%04X", (ADDR_BASE + (intr_num * VECTOR_SIZE)));
    std::string vector_address(vector_address_c);
    execution += std::to_string(current_time) + ", " + std::to_string(1) + ", find vector " + std::to_string(intr_num) + " in memory position " + vector_address + "\n";
    current_time += 1;
    // load address from vector table (strings in vectors vector)
    if ((size_t)intr_num < vectors.size()) {
        execution += std::to_string(current_time) + ", " + std::to_string(1) + ", load address " + vectors.at(intr_num) + " into the PC\n";
    } else {
        execution += std::to_string(current_time) + ", " + std::to_string(1) + ", load address 0x0000 into the PC\n";
    }
    current_time += 1;
    return {execution, current_time};
}

// lookup size in external_files
unsigned int get_size(std::string name, std::vector<external_file> external_files) {
    for (auto &f : external_files) {
        if (f.program_name == name) return f.size;
    }
    return (unsigned int)0;
}

// parse command line args: expected 4 args (trace vector_file device_file external_files)
// returns (vectors, delays, externals)
std::tuple<std::vector<std::string>, std::vector<int>, std::vector<external_file>> parse_args(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <trace.txt> <vector_table.txt> <device_table.txt> <external_files.txt>\n";
        exit(1);
    }

    // quick existence checks then read
    std::ifstream f;

    // check trace file accessible
    f.open(argv[1]); if(!f.is_open()){ std::cerr << "Error opening " << argv[1] << std::endl; exit(1);} f.close();

    // read vector table (argv[2]) -> each line is vector address string (ex: 0x0695)
    std::vector<std::string> vectors;
    f.open(argv[2]);
    if(!f.is_open()){ std::cerr << "Error opening " << argv[2] << std::endl; exit(1);}
    std::string line;
    while (std::getline(f, line)) {
        // assume each line is e.g. "2 0x0695" or just "0x0695" - take the last token
        auto parts = split_delim(line, " ");
        if (!parts.empty()) vectors.push_back(parts.back());
        else vectors.push_back("0x0000");
    }
    f.close();

    // read device delay file (argv[3]) -> each line one integer delay
    std::vector<int> delays;
    f.open(argv[3]);
    if(!f.is_open()){ std::cerr << "Error opening " << argv[3] << std::endl; exit(1);}
    while (std::getline(f,line)) {
        // skip blank lines
        if (line.empty()) continue;
        try {
            delays.push_back(std::stoi(line));
        } catch(...) {
            // non-numeric lines ignored
        }
    }
    f.close();

    // read external files list
    std::vector<external_file> external_files;
    f.open(argv[4]);
    if(!f.is_open()){ std::cerr << "Error opening " << argv[4] << std::endl; exit(1);}
    while (std::getline(f,line)) {
        if (line.empty()) continue;
        auto parts = split_delim(line, ",");
        external_file e;
        if (parts.size() >= 2) {
            // trim
            for (auto &s : parts) { while(!s.empty() && s.front()==' ') s.erase(0,1); while(!s.empty() && s.back()==' ') s.pop_back(); }
            e.program_name = parts[0];
            try { e.size = std::stoi(parts[1]); } catch(...) { e.size = 0; }
            external_files.push_back(e);
        }
    }
    f.close();

    return {vectors, delays, external_files};
}

// Writes string to file (overwrites)
void write_output(const std::string &execution, const char* filename) {
    std::ofstream output_file(filename);
    if (output_file.is_open()) {
        output_file << execution;
        output_file.close();
    } else {
        std::cerr << "Error opening " << filename << " for writing\n";
    }
}

#endif
