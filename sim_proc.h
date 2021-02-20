#ifndef SIM_PROC_H
#define SIM_PROC_H

#include <vector>
#include <algorithm>

typedef struct proc_params{
    unsigned long int rob_size;
    unsigned long int iq_size;
    unsigned long int width;
}proc_params;

//Additional Data Structures--------------------------------------------------------------------------------------------

//rep instr. bundle
//maintain timer encoding
class Instruction_Bundle{
public:
    int op_type, dst, src1, src2, latency, age_in_iss;
    int src1_non_rob, src2_non_rob, dst_non_rob;
    unsigned long pc;
    bool rs1_rob, rs2_rob, rs1_rdy, rs2_rdy;
    int FE_begin, DE_begin, RN_begin, RR_begin, DI_begin, IS_begin, EX_begin, WB_begin, RT_begin;
    int FE_cycles, DE_cycles, RN_cycles, RR_cycles, DI_cycles, IS_cycles, EX_cycles, WB_cycles, RT_cycles;
};

struct comparator{
    inline bool operator() (const Instruction_Bundle &temp1, const Instruction_Bundle &temp2){
        return (temp1.age_in_iss < temp2.age_in_iss);
    }
};

//Rename Map Table
class RMT{
private:
    class RMT_Entry{
    public:
        bool valid;
        int rob_tag;
    };
public:
    //67 registers in RISCV architecture
    RMT_Entry reg_list[67];

};
//Re-Order Buffer
class ROB_ENTRY{
public:
    unsigned int pc;
    int dest;
    bool rdy;
    ROB_ENTRY() : pc(0), dest(0), rdy(false){}
    void clr(){pc = 0; dest = 0; rdy = false;}
    bool empty(){
        return pc == 0 && dest == 0 && !rdy;
    }
};
class ROB{
public:
    int head, tail;
    unsigned long rob_size;
    std::vector<ROB_ENTRY> table;
    unsigned long space_available(){
        unsigned long avail_entries;
        //some entries in ROB, no 'wrap-around'
        if(tail < head){
            avail_entries = head - tail;
        }
        //some entries in ROB, 'wrap-around' occured at some point
        else if(head < tail){
            avail_entries = rob_size - (tail - head);
        }
        //first entries, full rob, or near full rob
        else{
            if(tail < (rob_size - 1)){
                if(table[tail+1].empty()){
                    avail_entries = rob_size;
                }
                else{
                    avail_entries = 0;
                }
            }
            else{
                if(table[tail-1].empty()){
                    avail_entries = rob_size;
                }
                else{
                    avail_entries = 0;
                }
            }
        }
        return avail_entries;
    };
};


//Issue Queue
class Issue_Queue{
public:
    unsigned long iq_size;
    std::vector<Instruction_Bundle> IQ;
};


//Globals---------------------------------------------------------------------------------------------------------------
int num_cycles, num_instr;


//Pipeline Stage Functions----------------------------------------------------------------------------------------------

//advance simulator cycle
//if pipeline is empty AND no more trace instr. then exit loop
bool Advance_Cycle(bool EOF_Flag, bool pipeline_empty){
    num_cycles++;
    return !(EOF_Flag && pipeline_empty);
    // continue simulation
}

//do nothing if no more trace instr. OR DE is not empty
//otherwise, fetch up to WIDTH instr from trace into DE
void Fetch(FILE *FP, unsigned long int width, std::vector<Instruction_Bundle> &decode_bundle, bool &EOF_flag){
    int op_type, dest, src1, src2;  // Variables are read from trace file
    unsigned int pc;           // Variable holds the pc read from input file
    if(decode_bundle.empty()) {
        for(int i = 0; i<width; i++){
            if (fscanf(FP, "%x %d %d %d %d", &pc, &op_type, &dest, &src1, &src2) != EOF) {
                num_instr += 1;
                Instruction_Bundle add_instr;
                add_instr.pc = pc;
                add_instr.rs1_rdy = add_instr.rs2_rdy = false;
                add_instr.rs1_rob = add_instr.rs2_rob = false;
                add_instr.op_type = op_type;
                add_instr.dst = add_instr.dst_non_rob = dest;
                add_instr.src1 = add_instr.src1_non_rob = src1;
                add_instr.src2 = add_instr.src2_non_rob = src2;
                add_instr.age_in_iss = 0;
                if(op_type == 0){
                    add_instr.latency = 1;
                }
                else if(op_type == 1){
                    add_instr.latency = 2;
                }
                else{
                    add_instr.latency = 5;
                }

                //timing information
                add_instr.FE_begin = num_cycles;
                add_instr.FE_cycles = 1;
                add_instr.DE_begin = num_cycles+1;

                //Move to DE
                decode_bundle.push_back(add_instr);
                EOF_flag = false;
            }
            else {
                EOF_flag = true;
            }
        }
    }
}

//if DE contains decode bundle:
//  - if RN is not empty
//      * do nothing
//  - if RN is empty
//      * advance decode bundle to RN
void Decode(std::vector<Instruction_Bundle> &rename_bundle, std::vector<Instruction_Bundle> &decode_bundle){
    if(!decode_bundle.empty() && rename_bundle.empty()){

        for(int i = 0; i < decode_bundle.size(); i++){
            //timing information
            decode_bundle.at(i).RN_begin = num_cycles+1;
            decode_bundle.at(i).DE_cycles = decode_bundle.at(i).RN_begin - decode_bundle.at(i).DE_begin;
        }
        //move to RN
        decode_bundle.swap(rename_bundle);
        decode_bundle.clear();
    }
};

//if RN contains rename bundle:
//  -if RR is not empty OR ROB is full
//      * do nothing
//  - if RR is empty AND ROB is open to entries
//      * process rename bundle and advance it to RR
void Rename(RMT &RMT_table, ROB &ROB_table, std::vector<Instruction_Bundle> &rename_bundle,
            std::vector<Instruction_Bundle> &regRead_bundle){
    if(!rename_bundle.empty() && regRead_bundle.empty()){
        //space unavailable in ROB
        if(ROB_table.space_available() < rename_bundle.size()){
            return;
        }
        //space available in ROB , process bundle
        else{
            for(int i = 0; i < rename_bundle.size(); i++){
                //allocate space in ROB
                ROB_table.table[ROB_table.tail].dest = rename_bundle.at(i).dst;
                ROB_table.table[ROB_table.tail].pc = rename_bundle.at(i).pc;
                ROB_table.table[ROB_table.tail].rdy = false;
                //src1 reg rename
                if(rename_bundle.at(i).src1 != -1){
                    if(RMT_table.reg_list[rename_bundle.at(i).src1].valid){
                        rename_bundle.at(i).src1 = RMT_table.reg_list[rename_bundle.at(i).src1].rob_tag;
                        rename_bundle.at(i).rs1_rob = true;
                    }
                }
                //src2 reg rename
                if(rename_bundle.at(i).src2 != -1){
                    if(RMT_table.reg_list[rename_bundle.at(i).src2].valid){
                        rename_bundle.at(i).src2 = RMT_table.reg_list[rename_bundle.at(i).src2].rob_tag;
                        rename_bundle.at(i).rs2_rob = true;
                    }
                }
                //dst rename
                if(rename_bundle.at(i).dst != -1){
                    RMT_table.reg_list[rename_bundle.at(i).dst].valid = true;
                    RMT_table.reg_list[rename_bundle.at(i).dst].rob_tag = ROB_table.tail;
                }
                rename_bundle.at(i).dst = ROB_table.tail;
                //update ROB pointers
                if(ROB_table.tail != (ROB_table.rob_size - 1)){
                    ROB_table.tail++;
                }
                else{
                    ROB_table.tail = 0;
                }
                //timing information
                rename_bundle.at(i).RR_begin = num_cycles + 1;
                rename_bundle.at(i).RN_cycles = rename_bundle.at(i).RR_begin - rename_bundle.at(i).RN_begin;
            }
            //move to RR
            rename_bundle.swap(regRead_bundle);
            rename_bundle.clear();
        }
    }
};

//if RR contains RR bundle:
//  -if DI is not empty
//      * do nothing
//  -if DI is empty
//      * process RR bundle and advance to DI
void RegRead(ROB &ROB_table, std::vector<Instruction_Bundle> &regRead_bundle,
        std::vector<Instruction_Bundle> &dispatch_bundle){
    if(!regRead_bundle.empty() && dispatch_bundle.empty()){
        //model readiness of src regs
        for(int i = 0; i < regRead_bundle.size(); i++){
            //src1
            if(regRead_bundle.at(i).rs1_rob) {
                if (ROB_table.table[regRead_bundle.at(i).src1].rdy) {
                    //dependency in ROB
                    regRead_bundle.at(i).rs1_rdy = true;
                }
            }
            else{
                //no dependencies
                regRead_bundle.at(i).rs1_rdy = true;
            }
            //src2
            if(regRead_bundle.at(i).rs2_rob) {
                if (ROB_table.table[regRead_bundle.at(i).src2].rdy) {
                    //dependency in ROB
                    regRead_bundle.at(i).rs2_rdy = true;
                }
            }
            else{
                //no dependencies
                regRead_bundle.at(i).rs2_rdy = true;
            }

            //update timing information
            regRead_bundle.at(i).DI_begin = num_cycles + 1;
            regRead_bundle.at(i).RR_cycles = regRead_bundle.at(i).DI_begin - regRead_bundle.at(i).RR_begin;
        }
        //move to DI
        regRead_bundle.swap(dispatch_bundle);
        regRead_bundle.clear();
    }
};

//if DI contains dispatch bundle:
//  -if # free IQ entries is < size of dispatch bundle in DI
//      * do nothing
//  -if # free IQ entries is >= size of dispatch bundle in DI
//      * dispatch all instr in DI to IQ
void Dispatch(std::vector<Instruction_Bundle> &dispatch_bundle, Issue_Queue &issueQueue){
    //check if IQ has available entries
    if(!dispatch_bundle.empty() && ((issueQueue.iq_size - issueQueue.IQ.size()) >= dispatch_bundle.size())){
        for(int i = 0; i < dispatch_bundle.size(); i++){
            //timing information
            dispatch_bundle.at(i).IS_begin = num_cycles + 1;
            dispatch_bundle.at(i).DI_cycles = dispatch_bundle.at(i).IS_begin - dispatch_bundle.at(i).DI_begin;
            //move to IQ
            issueQueue.IQ.push_back(dispatch_bundle.at(i));
        }
        dispatch_bundle.clear();
    }
};

//issue up to width oldest instructions from the IQ
void Issue(unsigned long width, std::vector<Instruction_Bundle> &execute_list, Issue_Queue &issueQueue){
    if(!issueQueue.IQ.empty()){
        std::sort(issueQueue.IQ.begin(), issueQueue.IQ.end(), comparator());
        int control = 1;
        int issued = 0;
        while(control != 0) {
            control = 0;
            for (int i = 0; i < issueQueue.IQ.size(); i++) {
                if (issueQueue.IQ.at(i).rs1_rdy && issueQueue.IQ.at(i).rs2_rdy) {
                    //timing information
                    issueQueue.IQ.at(i).EX_begin = num_cycles + 1;
                    issueQueue.IQ.at(i).IS_cycles = issueQueue.IQ.at(i).EX_begin - issueQueue.IQ.at(i).IS_begin;
                    //move to EX
                    execute_list.push_back(issueQueue.IQ.at(i));
                    issueQueue.IQ.erase(issueQueue.IQ.begin() + i);
                    issued++;
                    control++;
                    break;
                }
            }
            if (issued == width) { break; }
        }
    }
};

//from the execute_list, check for instr. finishing execution *this* cycle, then:
//  - remove instr from execute_list
//  - add instr to WB
//  - wakeup dependent instr. in the IQ/DI/RR stages (model readiness)
void Execute(std::vector<Instruction_Bundle> &execute_list, std::vector<Instruction_Bundle> &writeback_bundle, Issue_Queue &issueQueue,
             std::vector<Instruction_Bundle> &dispatch_bundle, std::vector<Instruction_Bundle> &regRead_bundle){
    if(!execute_list.empty()) {
        for (int j = 0; j < execute_list.size(); j++) {
            execute_list.at(j).latency--;
        }
        int temp = 1;
        while (temp != 0) {
            temp = 0;
            for (int i = 0; i < execute_list.size(); i++) {
                if (execute_list.at(i).latency == 0) {
                    //timing information
                    execute_list.at(i).WB_begin = num_cycles + 1;
                    execute_list.at(i).EX_cycles = execute_list.at(i).WB_begin - execute_list.at(i).EX_begin;
                    //move to WB
                    writeback_bundle.push_back(execute_list.at(i));

                    //wakeup - IQ
                    for (int j = 0; j < issueQueue.IQ.size(); j++) {
                        if (issueQueue.IQ.at(j).src1 == execute_list.at(i).dst) {
                            issueQueue.IQ.at(j).rs1_rdy = true;
                        }
                        if (issueQueue.IQ.at(j).src2 == execute_list.at(i).dst) {
                            issueQueue.IQ.at(j).rs2_rdy = true;
                        }
                    }
                    //wakeup - DI
                    for (int k = 0; k < dispatch_bundle.size(); k++) {
                        if (dispatch_bundle.at(k).src1 == execute_list.at(i).dst) {
                            dispatch_bundle.at(k).rs1_rdy = true;
                        }
                        if (dispatch_bundle.at(k).src2 == execute_list.at(i).dst) {
                            dispatch_bundle.at(k).rs2_rdy = true;
                        }
                    }
                    //wakeup - RR
                    for (int l = 0; l < regRead_bundle.size(); l++) {
                        if (regRead_bundle.at(l).src1 == execute_list.at(i).dst) {
                            regRead_bundle.at(l).rs1_rdy = true;
                        }
                        if (regRead_bundle.at(l).src2 == execute_list.at(i).dst) {
                            regRead_bundle.at(l).rs2_rdy = true;
                        }
                    }
                    //remove exec entry
                    execute_list.erase(execute_list.begin() + i);
                    temp++;
                    break;
                }
            }
        }
    }
};

//process WB bundle, mark instr 'ready' in ROB
void Writeback(std::vector<Instruction_Bundle> &writeback_bundle, ROB &ROB_table, std::vector<Instruction_Bundle> &retire_bundle){
    if(!writeback_bundle.empty()){
        for(int i = 0; i < writeback_bundle.size(); i++){
            //timing information
            writeback_bundle.at(i).RT_begin = num_cycles + 1;
            writeback_bundle.at(i).WB_cycles = writeback_bundle.at(i).RT_begin - writeback_bundle.at(i).WB_begin;
            //rdy in ROB
            ROB_table.table[writeback_bundle.at(i).dst].rdy = true;
            //move to RT
            retire_bundle.push_back(writeback_bundle.at(i));
        }
        writeback_bundle.clear();
    }
};

//retire up to WIDTH consecutive 'ready' instr from ROB head
//keep in mind RT->RR bypass
void Retire(unsigned long width, ROB &ROB_table, std::vector<Instruction_Bundle> &retire_bundle,
        std::vector<Instruction_Bundle> &regRead_bundle, RMT &RMT_table, int &seq){
    int num_retired = 0;
    while (num_retired < width) {
        //pointers point to same entry (first or last RT in sim)
        if ((ROB_table.tail == ROB_table.head) && (ROB_table.head != ROB_table.rob_size - 1)) {
            if (ROB_table.table[ROB_table.head + 1].pc == 0) {
                return;
            }
        }
        //rdy to RT
        if (ROB_table.table[ROB_table.head].rdy) {
            //RR bypass
            for (int i = 0; i < regRead_bundle.size(); i++) {
                if (ROB_table.head == regRead_bundle.at(i).src1) {
                    regRead_bundle.at(i).rs1_rdy = true;
                }
                if (ROB_table.head == regRead_bundle.at(i).src2) {
                    regRead_bundle.at(i).rs2_rdy = true;
                }
            }
            //clr entry in RMT
            for (int j = 0; j < 67; j++) {
                if (ROB_table.head == RMT_table.reg_list[j].rob_tag && RMT_table.reg_list[j].valid) {
                    RMT_table.reg_list[j].valid = false;
                    RMT_table.reg_list[j].rob_tag = 0;
                }
            }
            //retire entry in ROB - preserve program order
            for (int k = 0; k < retire_bundle.size(); k++) {
                if (ROB_table.table[ROB_table.head].pc == retire_bundle.at(k).pc) {
                    //timing information
                    retire_bundle.at(k).RT_cycles = (num_cycles + 1) - retire_bundle.at(k).RT_begin;

                    //print encoded timing information
                    //<seq_no> fu{<op_type>} src{<src1>,<src2>} dst{<dst>}
                    //FE{<begin-cycle>,<duration>} DE{…} RN{…} RR{…} DI{…} IS{…} EX{…}
                    //WB{…} RT{…}
                    printf("%d fu{%d} ", seq, retire_bundle.at(k).op_type);
                    printf("src{%d,%d} ", retire_bundle.at(k).src1_non_rob, retire_bundle.at(k).src2_non_rob);
                    printf("dst{%d} ", retire_bundle.at(k).dst_non_rob);
                    printf("FE{%d,%d} ", retire_bundle.at(k).FE_begin, retire_bundle.at(k).FE_cycles);
                    printf("DE{%d,%d} ", retire_bundle.at(k).DE_begin, retire_bundle.at(k).DE_cycles);
                    printf("RN{%d,%d} ", retire_bundle.at(k).RN_begin, retire_bundle.at(k).RN_cycles);
                    printf("RR{%d,%d} ", retire_bundle.at(k).RR_begin, retire_bundle.at(k).RR_cycles);
                    printf("DI{%d,%d} ", retire_bundle.at(k).DI_begin, retire_bundle.at(k).DI_cycles);
                    printf("IS{%d,%d} ", retire_bundle.at(k).IS_begin, retire_bundle.at(k).IS_cycles);
                    printf("EX{%d,%d} ", retire_bundle.at(k).EX_begin, retire_bundle.at(k).EX_cycles);
                    printf("WB{%d,%d} ", retire_bundle.at(k).WB_begin, retire_bundle.at(k).WB_cycles);
                    printf("RT{%d,%d}\n", retire_bundle.at(k).RT_begin, retire_bundle.at(k).RT_cycles);
                    seq++;
                    //clr RT instr
                    retire_bundle.erase(retire_bundle.begin() + k);
                    break;
                }
            }
            //retire entry in ROB - preserve program order cont.
            ROB_table.table[ROB_table.head].clr();
            //update ROB pointers
            if (ROB_table.head != (ROB_table.rob_size - 1)) {
                ROB_table.head++;
            } else {
                ROB_table.head = 0;
            }
            //control
            num_retired++;
        }
        else{
            return;
        }
    }
};
#endif