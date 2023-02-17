#ifndef __MACHINECODE_H__
#define __MACHINECODE_H__
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include <fstream>
#include "SymbolTable.h"

/* Hint:
 * MachineUnit: Compiler unit
 * MachineFunction: Function in assembly code
 * MachineInstruction: Single assembly instruction
 * MachineOperand: Operand in assembly instruction, such as immediate number, register, address label */

/* We only give the example code of "class BinaryMInstruction" and "class AccessMInstruction" (because we believe in you !!!),
 * You need to complete other the member function, especially "output()" ,
 * After that, you can use "output()" to print assembly code . */

class MachineUnit;
class MachineFunction;
class MachineBlock;
class MachineInstruction;
class MachineOperand;

bool isShifterOperandVal(unsigned bin_val);

class MachineOperand
{
private:
    MachineInstruction *parent;
    int type;
    double val;        // value of immediate number
    int reg_no;        // register no
    std::string label; // address label
    Type *valType;

public:
    enum
    {
        IMM,
        VREG,
        REG,
        LABEL
    };
    MachineOperand(int tp, double val, Type *valType = TypeSystem::intType);
    MachineOperand(std::string label);
    bool operator==(const MachineOperand &) const;
    bool operator<(const MachineOperand &) const;
    bool isImm() { return this->type == IMM; };
    bool isReg() { return this->type == REG; };
    bool isVReg() { return this->type == VREG; };
    bool isLabel() { return this->type == LABEL; };
    double getVal() { return this->val; };
    void setVal(double val) { this->val = val; }; // 目前仅用于函数参数（第四个以后）更新栈内偏移
    int getReg() { return this->reg_no; };
    void setReg(int regno)
    {
        this->type = REG;
        this->reg_no = regno;
    };
    std::string getLabel() { return this->label; };
    MachineInstruction *getParent() { return this->parent; };
    void setParent(MachineInstruction *p) { this->parent = p; };
    void printReg();
    void output();
    Type *getValType() { return this->valType; };
    bool isIllegalShifterOperand(); // 第二操作数应符合8位图格式
};

class MachineInstruction
{
protected:
    MachineBlock *parent;
    int no;
    int type;                            // Instruction type
    int cond = MachineInstruction::NONE; // Instruction execution condition, optional !!
    int op;                              // Instruction opcode
    // Instruction operand list, sorted by appearance order in assembly instruction
    std::vector<MachineOperand *> def_list;
    std::vector<MachineOperand *> use_list;
    void addDef(MachineOperand *ope) { def_list.push_back(ope); };
    void addUse(MachineOperand *ope) { use_list.push_back(ope); };
    // print execution code after printing opcode
    void printCond();
    enum instType
    {
        BINARY,
        LOAD,
        STORE,
        MOV,
        BRANCH,
        CMP,
        STACK,
        ZEXT,
        VCVT,
        VMRS
    };

public:
    enum condType
    {
        EQ,
        NE,
        LT,
        LE,
        GT,
        GE,
        NONE
    };
    virtual void output() = 0;
    virtual ~MachineInstruction();
    bool isBranch() { return type == BRANCH; };
    void setNo(int no) { this->no = no; };
    int getNo() { return no; };
    std::vector<MachineOperand *> &getDef() { return def_list; };
    std::vector<MachineOperand *> &getUse() { return use_list; };
    MachineBlock *getParent() { return parent; };
    int getOpType() { return op; };
};

class BinaryMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        ADD,
        SUB,
        MUL,
        DIV,
    };
    BinaryMInstruction(MachineBlock *p, int op,
                       MachineOperand *dst, MachineOperand *src1, MachineOperand *src2,
                       int cond = MachineInstruction::NONE);
    void output();
};

class LoadMInstruction : public MachineInstruction
{
public:
    LoadMInstruction(MachineBlock *p,
                     MachineOperand *dst, MachineOperand *src1, MachineOperand *src2 = nullptr,
                     int cond = MachineInstruction::NONE);
    void output();
};

class StoreMInstruction : public MachineInstruction
{
public:
    StoreMInstruction(MachineBlock *p,
                      MachineOperand *src1, MachineOperand *src2, MachineOperand *src3 = nullptr,
                      int cond = MachineInstruction::NONE);
    void output();
};

class MovMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        MOV,
        // MVN,
        // MOVT,
        VMOV,
        // VMOVF32
    };
    MovMInstruction(MachineBlock *p, int op,
                    MachineOperand *dst, MachineOperand *src,
                    int cond = MachineInstruction::NONE);
    void output();
};

class BranchMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        B,
        BL,
        // BX
    };
    BranchMInstruction(MachineBlock *p, int op,
                       MachineOperand *dst,
                       int cond = MachineInstruction::NONE);
    void output();
};

class CmpMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        CMP
    };
    CmpMInstruction(MachineBlock *p,
                    MachineOperand *src1, MachineOperand *src2,
                    int cond = MachineInstruction::NONE);
    void output();
};

class StackMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        PUSH,
        POP,
    };
    StackMInstruction(MachineBlock *p, int op,
                      MachineOperand *src,
                      int cond = MachineInstruction::NONE);
    StackMInstruction(MachineBlock *p, int op,
                      std::vector<MachineOperand *> src,
                      int cond = MachineInstruction::NONE);
    void output();
};

class ZextMInstruction : public MachineInstruction
{
public:
    ZextMInstruction(MachineBlock *p,
                     MachineOperand *dst, MachineOperand *src,
                     int cond = MachineInstruction::NONE);
    void output();
};

class VcvtMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        S2F,
        F2S
    };
    VcvtMInstruction(MachineBlock *p,
                     int op,
                     MachineOperand *dst,
                     MachineOperand *src,
                     int cond = MachineInstruction::NONE);
    void output();
};

class VmrsMInstruction : public MachineInstruction
{
public:
    VmrsMInstruction(MachineBlock *p);
    void output();
};

class MachineBlock
{
private:
    MachineFunction *parent;
    int no;
    std::vector<MachineBlock *> pred, succ;
    std::vector<MachineInstruction *> inst_list;
    std::set<MachineOperand *> live_in;
    std::set<MachineOperand *> live_out;

public:
    std::vector<MachineInstruction *> &getInsts() { return inst_list; };
    std::vector<MachineInstruction *>::iterator begin() { return inst_list.begin(); };
    std::vector<MachineInstruction *>::iterator end() { return inst_list.end(); };
    std::vector<MachineInstruction *>::reverse_iterator rbegin() { return inst_list.rbegin(); };
    std::vector<MachineInstruction *>::reverse_iterator rend() { return inst_list.rend(); };
    MachineBlock(MachineFunction *p, int no)
    {
        this->parent = p;
        this->no = no;
    };
    void insertInst(MachineInstruction *inst) { this->inst_list.push_back(inst); };
    void removeInst(MachineInstruction *inst) { inst_list.erase(std::find(inst_list.begin(), inst_list.end(), inst)); };
    void addPred(MachineBlock *p) { this->pred.push_back(p); };
    void addSucc(MachineBlock *s) { this->succ.push_back(s); };
    std::set<MachineOperand *> &getLiveIn() { return live_in; };
    std::set<MachineOperand *> &getLiveOut() { return live_out; };
    std::vector<MachineBlock *> &getPreds() { return pred; };
    std::vector<MachineBlock *> &getSuccs() { return succ; };
    MachineFunction *getParent() { return parent; };
    int getNo() { return no; };
    void insertBefore(MachineInstruction *pos, MachineInstruction *inst);
    void insertAfter(MachineInstruction *pos, MachineInstruction *inst);
    MachineOperand *insertLoadImm(MachineOperand *imm);
    void output();
    ~MachineBlock();
};

class MachineFunction
{
private:
    MachineUnit *parent;
    std::vector<MachineBlock *> block_list;
    int stack_size;
    std::set<int> saved_rregs;
    std::set<int> saved_sregs;
    SymbolEntry *sym_ptr;
    std::vector<MachineOperand *> additional_args_offset;

public:
    std::vector<MachineBlock *> &getBlocks() { return block_list; };
    std::vector<MachineBlock *>::iterator begin() { return block_list.begin(); };
    std::vector<MachineBlock *>::iterator end() { return block_list.end(); };
    MachineFunction(MachineUnit *p, SymbolEntry *sym_ptr);
    /* HINT:
     * Alloc stack space for local variable;
     * return current frame offset ;
     * we store offset in symbol entry of this variable in function AllocInstruction::genMachineCode()
     * you can use this function in LinearScan::genSpillCode() */
    int AllocSpace(int size)
    {
        this->stack_size += size;
        return this->stack_size;
    };
    void insertBlock(MachineBlock *block) { this->block_list.push_back(block); };
    void removeBlock(MachineBlock *block) { this->block_list.erase(std::find(block_list.begin(), block_list.end(), block)); };
    void addSavedRegs(int regno, bool is_sreg = false);
    std::vector<MachineOperand *> getSavedRRegs();
    std::vector<MachineOperand *> getSavedSRegs();
    MachineUnit *getParent() { return parent; };
    SymbolEntry *getSymPtr() { return sym_ptr; };
    void addAdditionalArgsOffset(MachineOperand *param) { additional_args_offset.push_back(param); };
    // std::vector<MachineOperand *> getAdditionalArgsOffset() { return additional_args_offset; };
    void output();
    ~MachineFunction();
};

class MachineUnit
{
private:
    std::vector<MachineFunction *> func_list;
    std::vector<IdentifierSymbolEntry *> global_var_list;
    int LtorgNo; // 当前LiteralPool序号

public:
    std::vector<MachineFunction *> &getFuncs() { return func_list; };
    std::vector<MachineFunction *>::iterator begin() { return func_list.begin(); };
    std::vector<MachineFunction *>::iterator end() { return func_list.end(); };
    void insertFunc(MachineFunction *func) { func_list.push_back(func); };
    void removeFunc(MachineFunction *func) { func_list.erase(std::find(func_list.begin(), func_list.end(), func)); };
    void insertGlobalVar(IdentifierSymbolEntry *sym_ptr) { global_var_list.push_back(sym_ptr); };
    void printGlobalDecl();
    void printBridge();
    int getLtorgNo() { return LtorgNo; };
    void output();
    ~MachineUnit()
    {
        auto delete_list = func_list;
        for (auto func : delete_list)
            delete func;
    }
};

#endif