#include <cstdio>
#include <cstdlib>
#include <list>
#include <vector>

#include "sim_proc.h"

/*  argc holds the number of command line arguments
    argv[] holds the commands themselves

    Example:-
    sim 256 32 4 gcc_trace.txt
    argc = 5
    argv[0] = "sim"
    argv[1] = "256"
    argv[2] = "32"
    ... and so on
*/

int main (int argc, char* argv[])
{
    FILE *FP;               // File handler
    char *trace_file;       // Variable that holds trace file name;
    proc_params params;       // look at sim_bp.h header file for the the definition of struct proc_params
    
    if (argc != 5)
    {
        printf("Error: Wrong number of inputs:%d\n", argc-1);
        exit(EXIT_FAILURE);
    }
    
    params.rob_size     = strtoul(argv[1], NULL, 10);
    params.iq_size      = strtoul(argv[2], NULL, 10);
    params.width        = strtoul(argv[3], NULL, 10);
    num_instr           = 0;
    num_cycles          = 0;
    trace_file          = argv[4];

    //**********************
    //Pipeline Registers
    std::vector<Instruction_Bundle> decode_bundle;
    std::vector<Instruction_Bundle> rename_bundle;
    std::vector<Instruction_Bundle> regRead_bundle;
    std::vector<Instruction_Bundle> dispatch_bundle;
    std::vector<Instruction_Bundle> writeback_bundle;
    std::vector<Instruction_Bundle> retire_bundle;
    std::vector<Instruction_Bundle> execute_list;
    //**********************
    ROB ROB_table;
    Issue_Queue issueQueue;
    RMT RMT_table;

    //**********************
    //Re-Order Buffer (ROB)
    ROB_table.head = ROB_table.tail = 3; //rob3
    ROB_table.rob_size = params.rob_size;
    ROB_ENTRY temp;
    for(int i = 0; i < params.rob_size; i++){
        ROB_table.table.push_back(temp);
    }
    //Issue_Queue issueQueue;
    issueQueue.iq_size = params.iq_size;

    //RMT
    for(int i = 0; i < 67; i++){
        RMT_table.reg_list[i].rob_tag = 0;
        RMT_table.reg_list[i].valid = false;
    }

    // Open trace_file in read mode
    FP = fopen(trace_file, "r");
    if(FP == NULL)
    {
        // Throw error and exit if fopen() failed
        printf("Error: Unable to open file %s\n", trace_file);
        exit(EXIT_FAILURE);
    }

    bool EOF_flag = false;
    bool pipeline_empty;
    int seq = 0;
    do {
        //true denotes current stage is empty or ready to accept new bundle
        //false denotes current stage has a bundle or can't accept new bundle
        Retire(params.width, ROB_table, retire_bundle, regRead_bundle, RMT_table, seq);
        Writeback(writeback_bundle, ROB_table, retire_bundle);
        Execute(execute_list, writeback_bundle, issueQueue, dispatch_bundle, regRead_bundle);
        Issue(params.width, execute_list, issueQueue);
        Dispatch(dispatch_bundle, issueQueue);
        RegRead(ROB_table, regRead_bundle, dispatch_bundle);
        Rename(RMT_table, ROB_table, rename_bundle, regRead_bundle);
        Decode(rename_bundle, decode_bundle);
        Fetch(FP, params.width, decode_bundle, EOF_flag);
        pipeline_empty = decode_bundle.empty() && rename_bundle.empty() && regRead_bundle.empty()
                && dispatch_bundle.empty() && issueQueue.IQ.empty() && ROB_table.head == ROB_table.tail && execute_list.empty()
                && writeback_bundle.empty();
    } while (Advance_Cycle(EOF_flag, pipeline_empty));



    printf("# === Simulator Command =========\n");
    printf("# ./sim %lu %lu %lu %s\n", params.rob_size, params.iq_size, params.width, trace_file);
    printf("# === Processor Configuration ===\n");
    printf("# ROB_SIZE = %lu\n", params.rob_size);
    printf("# IQ_SIZE  = %lu\n", params.iq_size);
    printf("# WIDTH    = %lu\n", params.width);
    printf("# === Simulation Results ========\n");
    printf("# Dynamic Instruction Count    = %d\n", num_instr);
    printf("# Cycles                       = %d\n", num_cycles);
    printf("# Instructions Per Cycle (IPC) = %2.2f\n", float(num_instr)/float(num_cycles));

    return 0;
}
