#ifndef __INSTRUCTION_H__
#define __INSTRUCTION_H__

#include "Operand.h"
#include "AsmBuilder.h"
#include <vector>
#include <map>
#include <sstream>

class BasicBlock;

class Instruction
{
public:
    Instruction(unsigned instType, BasicBlock *insert_bb = nullptr);
    virtual ~Instruction();
    BasicBlock *getParent();
    bool isLoad() const { return instType == LOAD; };
    bool isStore() const { return instType == STORE; };
    bool isUncond() const { return instType == UNCOND; };
    bool isCond() const { return instType == COND; };
    bool isRet() const { return instType == RET; };
    bool isAlloca() const { return instType == ALLOCA; };
    bool isPHI() const { return instType == PHI; };
    bool isCall() const { return instType == CALL; };
    bool isGep() const { return instType == GEP; };
    void setParent(BasicBlock *);
    void setNext(Instruction *);
    void setPrev(Instruction *);
    Instruction *getNext();
    Instruction *getPrev();
    virtual void output() const = 0;
    MachineOperand *genMachineOperand(Operand *);
    MachineOperand *genMachineReg(int reg, Type *valType = TypeSystem::intType);
    MachineOperand *genMachineVReg(Type *valType = TypeSystem::intType);
    MachineOperand *genMachineImm(double val, Type *valType = TypeSystem::intType);
    MachineOperand *genMachineLabel(int block_no);
    virtual void genMachineCode(AsmBuilder *) = 0;
    virtual std::vector<Operand *> &getDef() { return def_list; };
    virtual std::vector<Operand *> &getUses() { return use_list; };
    std::vector<Operand *> replaceAllUsesWith(Operand *); // Mem2Reg

protected:
    unsigned instType;
    unsigned opcode;
    Instruction *prev;
    Instruction *next;
    BasicBlock *parent;
    std::vector<Operand *> def_list; // size <= 1;
    std::vector<Operand *> use_list;
    // std::vector<Operand *> operands;
    enum
    {
        BINARY,
        COND,
        UNCOND,
        RET,
        LOAD,
        STORE,
        CMP,
        ALLOCA,
        ZEXT,
        IFCAST,
        CALL,
        PHI,
        GEP
    };
};

// meaningless instruction, used as the head node of the instruction list.
class DummyInstruction : public Instruction
{
public:
    DummyInstruction() : Instruction(-1, nullptr){};
    void output() const {};
    void genMachineCode(AsmBuilder *){};
};

class AllocaInstruction : public Instruction
{
public:
    AllocaInstruction(Operand *dst, SymbolEntry *se, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);

private:
    SymbolEntry *se;
};

class LoadInstruction : public Instruction
{
public:
    LoadInstruction(Operand *dst, Operand *src_addr, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);
};

// TODO : GepInstruction and getDef、getUses

class StoreInstruction : public Instruction
{
public:
    StoreInstruction(Operand *dst_addr, Operand *src, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);
};

class BinaryInstruction : public Instruction
{
public:
    BinaryInstruction(unsigned opcode, Operand *dst, Operand *src1, Operand *src2, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);
    enum
    {
        ADD,
        SUB,
        MUL,
        DIV,
        MOD
    };
};

class CmpInstruction : public Instruction
{
public:
    CmpInstruction(unsigned opcode, Operand *dst, Operand *src1, Operand *src2, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);
    enum
    {
        E,
        NE,
        L,
        LE,
        G,
        GE
    };
};

// unconditional branch
class UncondBrInstruction : public Instruction
{
public:
    UncondBrInstruction(BasicBlock *, BasicBlock *insert_bb = nullptr);
    void output() const;
    void setBranch(BasicBlock *);
    BasicBlock *getBranch();
    void genMachineCode(AsmBuilder *);

protected:
    BasicBlock *branch;
};

// conditional branch
class CondBrInstruction : public Instruction
{
public:
    CondBrInstruction(BasicBlock *, BasicBlock *, Operand *, BasicBlock *insert_bb = nullptr);
    void output() const;
    void setTrueBranch(BasicBlock *);
    BasicBlock *getTrueBranch();
    void setFalseBranch(BasicBlock *);
    BasicBlock *getFalseBranch();
    void genMachineCode(AsmBuilder *);

protected:
    BasicBlock *true_branch;
    BasicBlock *false_branch;
};

class RetInstruction : public Instruction
{
public:
    RetInstruction(Operand *src, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);
};

class ZextInstruction : public Instruction
{
public:
    ZextInstruction(Operand *dst, Operand *src, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);
};

class IntFloatCastInstruction : public Instruction
{
public:
    IntFloatCastInstruction(unsigned opcode, Operand *dst, Operand *src, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);
    enum
    {
        S2F,
        F2S
    };
};

class FuncCallInstruction : public Instruction
{
private:
    IdentifierSymbolEntry *func_se;

public:
    FuncCallInstruction(Operand *dst, std::vector<Operand *> params, IdentifierSymbolEntry *funcse, BasicBlock *insert_bb);
    void output() const;
    void genMachineCode(AsmBuilder *);
};

class PhiInstruction : public Instruction
{
private:
    std::map<BasicBlock *, Operand *> srcs;
    Operand *addr; // old PTR

public:
    PhiInstruction(Operand *dst, BasicBlock *insert_bb = nullptr);
    ~PhiInstruction();
    void output() const;
    void updateDst(Operand *);
    void addEdge(BasicBlock *block, Operand *src);
    Operand *getAddr() { return addr; };
    std::map<BasicBlock *, Operand *> &getSrcs() { return srcs; };

    void genMachineCode(AsmBuilder *){};
};

class GepInstruction : public Instruction
{
public:
    GepInstruction(Operand *dst, Operand *arr, std::vector<Operand *> idxList, BasicBlock *insert_bb = nullptr); // 普适，降维n-1次
    void output() const;
    void genMachineCode(AsmBuilder *);
};
#endif