#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <tuple>
#include <algorithm>
#include "interrupts.hpp"

struct ProcessRecord {
    PCB pcb;
    std::string state; // "running" or "waiting"
};

// Format snapshot for system status
std::string format_system_status(int current_time, const std::string& trace, const std::vector<ProcessRecord>& process_table) {
    std::ostringstream out;
    out << "time: " << current_time << "; current trace: " << trace << "\n";
    out << "+------------------------------------------------------+\n";
    out << "| PID | program name | partition number | size | state |\n";
    out << "+------------------------------------------------------+\n";
    for (const auto& r : process_table) {
        // print partition number as 1–6 (not 0–5)
        int printed_part_num = (r.pcb.partition_number >= 0) ? (r.pcb.partition_number + 1) : -1;
        out << "| " << r.pcb.PID << " | " << r.pcb.program_name << " | "
            << printed_part_num << " | " << r.pcb.size << " | "
            << r.state << " |\n";
    }
    out << "+------------------------------------------------------+\n\n";
    return out.str();
}

void append_to_file(const std::string& filename, const std::string& content) {
    std::ofstream out(filename, std::ios::app);
    if (out.is_open()) {
        out << content;
        out.close();
    } else {
        std::cerr << "Error writing to " << filename << std::endl;
    }
}

// Forward declaration
void process_trace_file(const std::string& filename,
                        int &current_time,
                        int &next_pid,
                        std::vector<ProcessRecord> &process_table,
                        std::vector<std::string> &vectors,
                        std::vector<int> &delays,
                        std::vector<external_file> &external_files,
                        std::string &execution_log);

void handle_activity_line(const std::string& rawline,
                          int &current_time,
                          int &next_pid,
                          std::vector<ProcessRecord> &process_table,
                          std::vector<std::string> &vectors,
                          std::vector<int> &delays,
                          std::vector<external_file> &external_files,
                          std::string &execution_log)
{
    auto tup = parse_trace(rawline);
    std::string activity = std::get<0>(tup);
    int dur = std::get<1>(tup);
    std::string program_name = std::get<2>(tup);

    if (process_table.empty()) return;

    if (activity == "CPU") {
        execution_log += std::to_string(current_time) + ", " + std::to_string(dur) + ", CPU Burst\n";
        current_time += dur;
    } else if (activity == "FORK") {
        int vector_num = 2;
        auto p = intr_boilerplate(current_time, vector_num, 10, vectors);
        execution_log += p.first;
        current_time = p.second;

        execution_log += std::to_string(current_time) + ", " + std::to_string(dur) + ", cloning the PCB\n";
        current_time += dur;

        PCB parent = process_table[0].pcb;
        PCB child = parent;
        child.PID = next_pid++;
        child.PPID = parent.PID;
        process_table[0].state = "waiting";
        process_table.insert(process_table.begin(), {child, "running"});

        execution_log += std::to_string(current_time) + ", 0, scheduler called\n";
        execution_log += std::to_string(current_time) + ", 1, IRET\n";
        current_time += 1;

        // ✅ Write system snapshot immediately
        append_to_file("system_status.txt", format_system_status(current_time, rawline, process_table));

    } else if (activity == "EXEC") {
        int vector_num = 3;
        auto p = intr_boilerplate(current_time, vector_num, 10, vectors);
        execution_log += p.first;
        current_time = p.second;

        unsigned int prog_size = get_size(program_name, external_files);
        int loader_time = 15 * prog_size;

        PCB &running = process_table[0].pcb;
        free_memory(&running);
        running.program_name = program_name;
        running.size = prog_size;
        allocate_memory(&running);

        execution_log += std::to_string(current_time) + ", " + std::to_string(dur) + ", EXEC " + program_name + "\n";
        current_time += dur;
        execution_log += std::to_string(current_time) + ", " + std::to_string(loader_time) + ", loading program into memory\n";
        current_time += loader_time;
        execution_log += std::to_string(current_time) + ", 3, marking partition as occupied\n";
        current_time += 3;
        execution_log += std::to_string(current_time) + ", 6, updating PCB\n";
        current_time += 6;
        execution_log += std::to_string(current_time) + ", 0, scheduler called\n";
        execution_log += std::to_string(current_time) + ", 1, IRET\n";
        current_time += 1;

        // ✅ Write system snapshot immediately
        append_to_file("system_status.txt", format_system_status(current_time, rawline, process_table));

        // Run the program trace
        std::string prog_trace = program_name + ".txt";
        process_trace_file(prog_trace, current_time, next_pid, process_table, vectors, delays, external_files, execution_log);
    }
}

void process_trace_file(const std::string& filename,
                        int &current_time,
                        int &next_pid,
                        std::vector<ProcessRecord> &process_table,
                        std::vector<std::string> &vectors,
                        std::vector<int> &delays,
                        std::vector<external_file> &external_files,
                        std::string &execution_log)
{
    std::ifstream infile(filename);
    if (!infile.is_open()) return;

    std::string line;
    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        handle_activity_line(line, current_time, next_pid, process_table, vectors, delays, external_files, execution_log);
    }
    infile.close();
}

int main(int argc, char* argv[]) {
    auto args_tuple = parse_args(argc, argv);
    auto vectors = std::get<0>(args_tuple);
    auto delays = std::get<1>(args_tuple);
    auto external_files = std::get<2>(args_tuple);

    std::string execution_log;
    int current_time = 0;
    int next_pid = 1;

    PCB initPCB(0, -1, "init", 1, 5);
    memory[5].code = "init";
    std::vector<ProcessRecord> process_table = {{initPCB, "running"}};

    // clear any old status file
    std::ofstream clear("system_status.txt", std::ios::trunc);
    clear.close();

    process_trace_file(argv[1], current_time, next_pid, process_table, vectors, delays, external_files, execution_log);

    write_output(execution_log, "execution.txt");
    std::cout << "Simulation complete.\n";
}
