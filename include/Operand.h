#ifndef __OPERAND_H__
#define __OPERAND_H__

#include "SymbolTable.h"
#include <vector>
#include <algorithm>
#include <assert.h>

class Instruction;
class Function;

// class Operand - The operand of an instruction.
class Operand
{
    typedef std::set<Instruction *>::iterator use_iterator;

private:
    std::set<Instruction *> uses; // Intructions that use this operand.
    std::set<Instruction *> defs; // The instruction where this operand is defined.
    SymbolEntry *se;              // The symbol entry of this operand.
public:
    Operand(SymbolEntry *se) : se(se)
    {
        defs = std::set<Instruction *>();
        uses = std::set<Instruction *>();
    };
    const std::set<Instruction *> &Defs() const { return defs; };
    void setDef(Instruction *inst) { defs = std::set<Instruction *>{inst}; };
    void addDef(Instruction *inst) { defs.insert(inst); }; // 特例是消除PHI产生的add ..., ..., 0，会有多个Def
    void removeDef(Instruction *inst) { defs.erase(inst); };
    void addUse(Instruction *inst) { uses.insert(inst); };
    void removeUse(Instruction *inst) { uses.erase(inst); };
    int usersNum() const { return uses.size(); };
    int defsNum() const { return defs.size(); };
    // bool operator==(const Operand &) const;
    // bool operator<(const Operand &) const;

    use_iterator use_begin() { return uses.begin(); };
    use_iterator use_end() { return uses.end(); };
    Type *getType() { return se->getType(); };
    std::string toStr() const;
    Instruction *getDef()
    {
        assert(defs.size() <= 1);
        return defs.size() == 1 ? *(defs.begin()) : nullptr;
    };
    std::set<Instruction *> getUses() { return uses; };
    SymbolEntry *getEntry() { return se; };
};

#endif