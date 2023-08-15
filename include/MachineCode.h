#ifndef __MACHINECODE_H__
#define __MACHINECODE_H__
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include <unordered_map>
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
bool isSignedShifterOperandVal(signed val);
bool is_Legal_VMOV_FloatImm(float val);

class MachineOperand
{
private:
    MachineInstruction *parent;
    int type;          // {IMM, VREG, REG, LABEL}
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
    bool isAddrForThreadsFunc;
    std::string addrForThreadsFunc;
    MachineOperand(int tp, double val, Type *valType = TypeSystem::intType);
    MachineOperand(std::string label);
    bool operator==(const MachineOperand &) const;
    bool operator<(const MachineOperand &) const;
    bool isImm() const { return this->type == IMM; };
    bool isReg() const { return this->type == REG; };
    bool isVReg() const { return this->type == VREG; };
    bool isLabel() const { return this->type == LABEL; };
    double getVal() const { return this->val; };
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
    void output();
    std::string toStr();
    Type *getValType()
    {
        assert(valType->isInt() || valType->isFloat());
        return this->valType;
    };
    bool isIllegalShifterOperand(); // 第二操作数应符合8位图格式
};

class MachineInstruction
{
protected:
    MachineBlock *parent;
    int no;
    int type;                            // Instruction type
    int cond = MachineInstruction::NONE; // Instruction execution condition, optional !!
    int op = -1;                         // Instruction opcode
    // Instruction operand list, sorted by appearance order in assembly instruction
    std::vector<MachineOperand *> def_list;
    std::vector<MachineOperand *> use_list;
    // print execution code after printing opcode
    void printCond();

public:
    enum instType
    {
        DUMMY,
        BINARY,
        LOAD,
        STORE,
        MOV,
        BRANCH,
        CMP,
        STACK,
        ZEXT,
        VCVT,
        VMRS,
        // SMULL,
        MLAS,
        VMLAS
    };
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
    void setNo(int no) { this->no = no; };
    int getNo() { return no; };
    int getCond() { return cond; };
    std::vector<MachineOperand *> &getDef() { return def_list; };
    std::vector<MachineOperand *> &getUse() { return use_list; };
    void addDef(MachineOperand *ope)
    {
        def_list.push_back(ope);
        if (ope->getParent() == nullptr)
            ope->setParent(this);
    };
    void addUse(MachineOperand *ope)
    {
        use_list.push_back(ope);
        if (ope->getParent() == nullptr)
            ope->setParent(this);
    };
    MachineBlock *getParent() { return parent; };
    void setParent(MachineBlock *block) { this->parent = block; }
    int getOpType() { return op; };
    int getInstType() const { return type; };

    bool isCritical() const;

    virtual MachineInstruction *deepCopy() = 0;

    bool isDummy() const { return type == DUMMY; };
    bool isAdd() const;
    bool isAddShift() const;
    bool isBranch() const;
    bool isStack() const { return type == STACK; };
    bool isPush() const;
    bool isVPush() const;
    bool isPop() const;
    bool isVPop() const;
    bool isBinary() const { return type == BINARY; };
    bool isSub() const;
    bool isSubShift() const;
    bool isRsb() const;
    bool isMul() const;
    bool isDiv() const;
    bool isStore() const { return type == STORE; };
    bool isLoad() const { return type == LOAD; };
    bool isMov() const;
    bool isVmov() const;
    bool isMovShift() const;
    bool isBL() const;
    bool isZext() const { return type == ZEXT; };
    bool isCondMov() const;
    // bool isSmull() const;
};

// 放在函数开头和结尾，分别假装定义函数参数对应的物理寄存器和使用函数返回值r0/s0，从而便于生存期等处理，防止被误判为死代码消除
class DummyMInstruction : public MachineInstruction
{
public:
    DummyMInstruction(MachineBlock *p,
                      std::vector<MachineOperand *> defs, std::vector<MachineOperand *> uses,
                      int cond = MachineInstruction::NONE);
    void output();
    MachineInstruction* deepCopy();
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
        AND,
        RSB,
        ADDLSL,
        ADDLSR,
        ADDASR,
        SUBLSL,
        SUBLSR,
        SUBASR,
        RSBLSL,
        RSBLSR,
        RSBASR
    };
    BinaryMInstruction(MachineBlock *p, int op,
                       MachineOperand *dst, MachineOperand *src1, MachineOperand *src2,
                       MachineOperand *shifter = nullptr,
                       int cond = MachineInstruction::NONE);
    void output();
    MachineInstruction* deepCopy();
};

class LoadMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        LOADLSL,
        LOADLSR,
        LOADASR
    };
    LoadMInstruction(MachineBlock *p,
                     MachineOperand *dst, MachineOperand *src1, MachineOperand *src2 = nullptr,
                     int op = -1, MachineOperand *shifter = nullptr,
                     int cond = MachineInstruction::NONE);
    void output();
    MachineInstruction* deepCopy();
};

class StoreMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        STORELSL,
        STORELSR,
        STOREASR
    };
    StoreMInstruction(MachineBlock *p,
                      MachineOperand *src1, MachineOperand *src2, MachineOperand *src3 = nullptr,
                      int op = -1, MachineOperand *shifter = nullptr,
                      int cond = MachineInstruction::NONE);
    void output();
    MachineInstruction* deepCopy();
};

class MovMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        MOV,
        // MVN,
        // MOVT,
        MOVLSL,
        MOVLSR,
        MOVASR,
        VMOV
    };
    MovMInstruction(MachineBlock *p, int op,
                    MachineOperand *dst, MachineOperand *src,
                    MachineOperand *shifter = nullptr,
                    int cond = MachineInstruction::NONE);
    void output();
    MachineInstruction* deepCopy();
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
    MachineInstruction* deepCopy();
};

class CmpMInstruction : public MachineInstruction
{
public:
    CmpMInstruction(MachineBlock *p,
                    MachineOperand *src1, MachineOperand *src2,
                    int cond = MachineInstruction::NONE);
    void output();
    MachineInstruction* deepCopy();
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
    MachineInstruction* deepCopy();
};

class ZextMInstruction : public MachineInstruction
{
public:
    ZextMInstruction(MachineBlock *p,
                     MachineOperand *dst, MachineOperand *src,
                     int cond = MachineInstruction::NONE);
    void output();
    MachineInstruction* deepCopy();
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
    MachineInstruction* deepCopy();
};

class VmrsMInstruction : public MachineInstruction
{
public:
    VmrsMInstruction(MachineBlock *p);
    void output();
    MachineInstruction* deepCopy();
};

// class SmullMInstruction : public MachineInstruction
// {
// public:
//     SmullMInstruction(MachineBlock *p,
//                       MachineOperand *dst1,
//                       MachineOperand *dst2,
//                       MachineOperand *src1,
//                       MachineOperand *src2,
//                       int cond = MachineInstruction::NONE);
//     void output();
// };

class MLASMInstruction : public MachineInstruction
{
public:
    enum opType
    {
        MLA,
        MLS
    };
    MLASMInstruction(MachineBlock *p,
                     int op,
                     MachineOperand *dst,
                     MachineOperand *src1,
                     MachineOperand *src2,
                     MachineOperand *src3,
                     int cond = MachineInstruction::NONE);
    void output();
    MachineInstruction* deepCopy();
};

// class VMLASMInstruction : public MachineInstruction
// {
// public:
//     enum opType
//     {
//         VMLA,
//         VMLS
//     };
//     VMLASMInstruction(MachineBlock *p,
//                       int op,
//                       MachineOperand *dst,
//                       MachineOperand *src1,
//                       MachineOperand *src2);
//     void output();
// };

class MachineBlock
{
private:
    MachineFunction *parent;
    int no;
    std::vector<MachineBlock *> preds, succs;
    std::vector<MachineInstruction *> inst_list;
    std::set<MachineOperand *> live_in;
    std::set<MachineOperand *> live_out;
    std::set<MachineBlock *> SDoms;
    MachineBlock *IDom;

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
        this->IDom = nullptr;
    };
    void insertBack(MachineInstruction *inst)
    {
        this->inst_list.push_back(inst);
        inst->setParent(this);
    };
    void removeInst(MachineInstruction *inst)
    {
        auto iter = std::find(inst_list.begin(), inst_list.end(), inst);
        if (iter != inst_list.end())
        {
            inst_list.erase(iter);
            inst->setParent(nullptr);
        };
    }
    void addPred(MachineBlock *p) { this->preds.push_back(p); };
    void addSucc(MachineBlock *s) { this->succs.push_back(s); };
    void removePred(MachineBlock *p)
    {
        auto iter = std::find(this->preds.begin(), this->preds.end(), p);
        if (iter != this->preds.end())
            this->preds.erase(iter);
    }
    void removeSucc(MachineBlock *s)
    {
        auto iter = std::find(this->succs.begin(), this->succs.end(), s);
        if (iter != this->succs.end())
            this->succs.erase(iter);
    }
    std::set<MachineOperand *> &getLiveIn() { return live_in; };
    std::set<MachineOperand *> &getLiveOut() { return live_out; };
    std::vector<MachineBlock *> &getPreds() { return preds; };
    std::vector<MachineBlock *> &getSuccs() { return succs; };
    MachineFunction *getParent() { return parent; };
    int getNo() { return no; };
    void insertBefore(MachineInstruction *pos, MachineInstruction *inst);
    void insertAfter(MachineInstruction *pos, MachineInstruction *inst);
    MachineOperand *insertLoadImm(MachineOperand *imm);
    void output();
    std::set<MachineBlock *> &getSDoms() { return SDoms; };
    MachineBlock *&getIDom() { return IDom; };
    ~MachineBlock();

    // MDCE
    MachineInstruction *getNext(MachineInstruction *instr);
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
    MachineBlock *entry;
    std::set<MachineOperand *> additional_args_offset;
    bool largeStack;
    std::map<MachineOperand, std::set<MachineOperand *>> all_uses;

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
        assert(stack_size % 4 == 0);
        return this->stack_size;
    };
    bool hasLargeStack() { return largeStack; };
    void setLargeStack() { largeStack = true; };
    void insertFront(MachineBlock *block) { this->block_list.insert(this->block_list.begin(), block); };
    void insertBlock(MachineBlock *block) { this->block_list.push_back(block); };
    void removeBlock(MachineBlock *block) { this->block_list.erase(std::find(block_list.begin(), block_list.end(), block)); };
    void addSavedRegs(int regno, bool is_sreg = false);
    std::vector<MachineOperand *> getSavedRRegs();
    std::vector<MachineOperand *> getSavedSRegs();
    MachineUnit *getParent() { return parent; };
    SymbolEntry *getSymPtr() { return sym_ptr; };
    void addAdditionalArgsOffset(MachineOperand *param) { additional_args_offset.insert(param); };
    std::set<MachineOperand *> getAdditionalArgsOffset() { return additional_args_offset; };
    MachineBlock *getEntry() { return entry; };
    void setEntry(MachineBlock *entry) { this->entry = entry; };
    void AnalyzeLiveVariable();
    std::map<MachineOperand, std::set<MachineOperand *>> &getAllUses() { return all_uses; };
    void outputStart();
    void outputEnd();
    void output();
    void computeDom();
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
    void printThreadFuncs(int num);
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