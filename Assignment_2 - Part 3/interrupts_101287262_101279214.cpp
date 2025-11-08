#include<interrupts.hpp>
#include <cstdlib>
#include <ctime>

std::tuple<std::string, std::string, int>
simulate_trace(std::vector<std::string> trace_file, int time,
               std::vector<std::string> vectors, std::vector<int> delays,
               std::vector<external_file> external_files, PCB current,
               std::vector<PCB> wait_queue) 
{
    std::string execution = "";
    std::string system_status = "";
    int current_time = time;

    for(size_t i = 0; i < trace_file.size(); i++) {
        auto trace = trace_file[i];
        auto [activity, duration_intr, program_name] = parse_trace(trace);

        if(activity == "CPU") {
            execution += std::to_string(current_time) + ", " + std::to_string(duration_intr) + ", CPU Burst\n";
            current_time += duration_intr;
        } 
        else if(activity == "FORK") {
            auto [intr, t1] = intr_boilerplate(current_time, 2, 10, vectors);
            execution += intr;
            current_time = t1;

            execution += std::to_string(current_time) + ", " + std::to_string(duration_intr) + ", cloning the PCB\n";
            current_time += duration_intr;

            execution += std::to_string(current_time) + ", 0, scheduler called\n";
            execution += std::to_string(current_time) + ", 1, IRET\n";
            current_time += 1;

            PCB child = current;
            child.PID = wait_queue.size() + 1;
            if (!allocate_memory(&child)) {
                std::cerr << "ERROR! Memory allocation failed for child!" << std::endl;
            }

            std::vector<PCB> parent_wait_q = wait_queue;
            parent_wait_q.push_back(current);

            system_status += "time: " + std::to_string(current_time) + "; current trace: FORK, " + std::to_string(duration_intr) + "\n";
            system_status += print_PCB(child, parent_wait_q);

            // Parse child/parent blocks after the FORK using IF_CHILD/IF_PARENT/ENDIF
            std::vector<std::string> child_trace;
            bool skip = true;
            bool exec_flag = false;
            int parent_index = 0;

            for(size_t j = i; j < trace_file.size(); j++) {
                auto [_activity, _duration, _pn] = parse_trace(trace_file[j]);
                if(skip && _activity == "IF_CHILD") {
                    skip = false;
                    continue;
                } else if(_activity == "IF_PARENT"){
                    skip = true;
                    parent_index = j;
                    if(exec_flag) { break; }
                } else if(skip && _activity == "ENDIF") {
                    skip = false;
                    continue;
                } else if(!skip && _activity == "EXEC") {
                    skip = true;
                    child_trace.push_back(trace_file[j]);
                    exec_flag = true;
                }
                if(!skip) {
                    child_trace.push_back(trace_file[j]);
                }
            }
            i = parent_index;

            auto [child_exec, child_sys, child_end_time] = 
                simulate_trace(child_trace, current_time, vectors, delays, external_files, child, parent_wait_q);

            execution      += child_exec;
            system_status  += child_sys;
            current_time    = child_end_time;
        } 
        else if(activity == "EXEC") {
            auto [intr, t1] = intr_boilerplate(current_time, 3, 10, vectors);
            current_time = t1;
            execution += intr;

            int prog_mb = get_size(program_name, external_files);
            if (prog_mb < 0) {
                std::cerr << "ERROR! Program '" << program_name << "' not found in external_files.txt\n";
                prog_mb = 0;
            }
            execution += std::to_string(current_time) + ", " + std::to_string(duration_intr) + ", Program is " + std::to_string(prog_mb) + " Mb large\n";
            current_time += duration_intr;

            int load_ms = prog_mb * 15;
            execution += std::to_string(current_time) + ", " + std::to_string(load_ms) + ", loading program into memory\n";
            current_time += load_ms;

            int mark_ms = 3;
            execution += std::to_string(current_time) + ", " + std::to_string(mark_ms) + ", marking partition as occupied\n";
            current_time += mark_ms;

            current.program_name = program_name;
            current.size = prog_mb;
            current.partition_number = -1;
            int up_ms = 6;
            if (!allocate_memory(&current)) {
                std::cerr << "ERROR! Memory allocation failed on EXEC for '" << program_name << "'\n";
            }
            execution += std::to_string(current_time) + ", " + std::to_string(up_ms) + ", updating PCB\n";
            current_time += up_ms;

            system_status += "time: " + std::to_string(current_time) + "; current trace: EXEC " + program_name + ", " + std::to_string(duration_intr) + "\n";
            system_status += print_PCB(current, wait_queue);

            execution += std::to_string(current_time) + ", 0, scheduler called\n";
            execution += std::to_string(current_time) + ", 1, IRET\n";
            current_time += 1;

            std::ifstream exec_trace_file(program_name + ".txt");
            std::vector<std::string> exec_traces;
            std::string exec_trace;
            while(std::getline(exec_trace_file, exec_trace)) {
                exec_traces.push_back(exec_trace);
            }
            if (!exec_traces.empty()) {
                auto [sub_exec, sub_sys, sub_end_time] =
                    simulate_trace(exec_traces, current_time, vectors, delays, external_files, current, wait_queue);
                execution += sub_exec;
                system_status += sub_sys;
                current_time = sub_end_time;
            }
            break; //Why is this important? (answer in report)
        }
    }
    return {execution, system_status, current_time};
}

int main(int argc, char** argv) {
    auto [vectors, delays, external_files] = parse_args(argc, argv);
    std::ifstream input_file(argv[1]);
    print_external_files(external_files);

    PCB current(0, -1, "init", 1, -1);
    if(!allocate_memory(&current)) {
        std::cerr << "ERROR! Memory allocation failed!" << std::endl;
    }
    std::vector<PCB> wait_queue;
    std::srand(time(nullptr));

    std::vector<std::string> trace_file;
    std::string trace;
    while(std::getline(input_file, trace)) {
        trace_file.push_back(trace);
    }
    input_file.close();

    std::ofstream clear("system_status.txt", std::ios::trunc); clear.close();
    auto [execution, system_status, _] = simulate_trace(trace_file, 0, vectors, delays, external_files, current, wait_queue);

    write_output(execution, "execution.txt");
    write_output(system_status, "system_status.txt");

    std::cout << "Simulation complete.\n";
    return 0;
}
