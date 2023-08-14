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
    bool isBinary() const { return instType == BINARY; };
    bool isCmp() const { return instType == CMP; };
    bool isDummy() const { return instType == -1; };
    void setParent(BasicBlock *);
    void setNext(Instruction *);
    void setPrev(Instruction *);
    Instruction *getNext();
    Instruction *getPrev();
    Instruction* replaceWith(Instruction *new_inst);
    virtual void output() const = 0;
    MachineOperand *genMachineOperand(Operand *);
    MachineOperand *genMachineReg(int reg, Type *valType = TypeSystem::intType);
    MachineOperand *genMachineVReg(Type *valType = TypeSystem::intType);
    MachineOperand *genMachineImm(double val, Type *valType = TypeSystem::intType);
    MachineOperand *genMachineLabel(int block_no);
    virtual void genMachineCode(AsmBuilder *) = 0;
    virtual Operand *&getDef()
    {
        assert(!def_list.empty());
        return def_list[0];
    };
    bool hasNoDef() { return def_list.empty(); };
    bool hasNoUse() { return use_list.empty(); };
    virtual std::vector<Operand *> &getUses() { return use_list; };
    void replaceAllUsesWith(Operand *);                                                   // replace all uses of the def
    void replaceUsesWith(Operand *old_op, Operand *new_op, BasicBlock *pre_bb = nullptr); // replace uses of this instruction
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
    unsigned getInstType() const { return instType; };
    unsigned getOpcode() const { return opcode; };

    // DCE
    bool isCritical();

    // Autoinline
    virtual Instruction *copy() { return nullptr; };
    virtual void setDef(Operand *def)
    {
        assert(def_list.size() == 1);
        def_list[0]->removeDef(this);
        def_list[0] = def;
        def->setDef(this);
    };

    // MemOpt
    virtual bool constEval() { return false; };

protected:
    unsigned instType;
    unsigned opcode;
    Instruction *prev;
    Instruction *next;
    BasicBlock *parent;
    std::vector<Operand *> def_list; // size <= 1;
    std::vector<Operand *> use_list;

    // std::vector<Operand *> operands;
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
    SymbolEntry *getSymPtr() { return se; };

    // Autoinline
    Instruction *copy() { return new AllocaInstruction(*this); };

private:
    SymbolEntry *se;
};

class LoadInstruction : public Instruction
{
public:
    LoadInstruction(Operand *dst, Operand *src_addr, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);

    // Autoinline
    Instruction *copy() { return new LoadInstruction(*this); };
};

class StoreInstruction : public Instruction
{
public:
    StoreInstruction(Operand *dst_addr, Operand *src, BasicBlock *insert_bb = nullptr);
    void output() const;
    void genMachineCode(AsmBuilder *);

    // Autoinline
    Instruction *copy() { return new StoreInstruction(*this); };
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

    // Autoinline
    Instruction *copy() { return new BinaryInstruction(*this); };
    // MemOpt
    bool constEval();
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

    // Autoinline
    Instruction *copy() { return new CmpInstruction(*this); };
    // MemOpt
    bool constEval();
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

    // Autoinline
    Instruction *copy() { return new UncondBrInstruction(*this); };

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

    // Autoinline
    Instruction *copy() { return new CondBrInstruction(*this); };
    // MemOpt
    bool constEval();

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

    // Autoinline
    Instruction *copy() { return new ZextInstruction(*this); };
    // MemOpt
    bool constEval();
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

    // Autoinline
    Instruction *copy() { return new IntFloatCastInstruction(*this); };
    // MemOpt
    bool constEval();
};

class FuncCallInstruction : public Instruction
{
private:
    IdentifierSymbolEntry *func_se;

public:
    FuncCallInstruction(Operand *dst, std::vector<Operand *> params, IdentifierSymbolEntry *funcse, BasicBlock *insert_bb=nullptr);
    void output() const;
    IdentifierSymbolEntry *getFuncSe() { return func_se; };
    void genMachineCode(AsmBuilder *);

    // Autoinline
    Instruction *copy() { return new FuncCallInstruction(*this); };
};

class PhiInstruction : public Instruction
{
private:
    std::map<BasicBlock *, Operand *> srcs;
    Operand *addr; // old PTR
    bool incomplete;

public:
    PhiInstruction(Operand *dst, bool incomplete = false, BasicBlock *insert_bb = nullptr);
    void output() const;
    void updateDst(Operand *);
    void addEdge(BasicBlock *block, Operand *src);
    void removeEdge(BasicBlock *block);
    void replaceEdge(BasicBlock *block, Operand *replVal);
    Operand *getAddr() { return addr; };
    std::map<BasicBlock *, Operand *> &getSrcs() { return srcs; };
    bool &get_incomplete() { return this->incomplete; };

    void genMachineCode(AsmBuilder *){};

    // Autoinline
    Instruction *copy() { return new PhiInstruction(*this); };
    // MemOpt
    bool constEval();
};

class GepInstruction : public Instruction
{
public:
    GepInstruction(Operand *dst, Operand *arr, std::vector<Operand *> idxList, BasicBlock *insert_bb = nullptr); // 普适，降维n-1次
    void output() const;
    void genMachineCode(AsmBuilder *);

    // Autoinline
    Instruction *copy() { return new GepInstruction(*this); };
};
#endif