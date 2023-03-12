#ifndef __BASIC_BLOCK_H__
#define __BASIC_BLOCK_H__
#include <vector>
#include <set>
#include "Instruction.h"
#include "AsmBuilder.h"

class Function;

class BasicBlock
{
    typedef std::set<BasicBlock *>::iterator bb_iterator;

private:
    std::set<BasicBlock *> pred, succ;
    Instruction *head;
    Function *parent;
    int no;
    std::set<Operand *> live_in;
    std::set<Operand *> live_out;
    std::set<BasicBlock *> SDoms;
    BasicBlock *IDom;
    std::set<BasicBlock *> DomFrontiers;

public:
    BasicBlock(Function *);
    ~BasicBlock();
    std::set<Operand *> &getLiveIn() { return live_in; };
    std::set<Operand *> &getLiveOut() { return live_out; };
    std::set<BasicBlock *> &getSDoms() { return SDoms; };
    BasicBlock *&getIDom() { return IDom; };
    std::set<BasicBlock *> &getDomFrontiers() { return DomFrontiers; };
    void insertFront(Instruction *);
    void insertBack(Instruction *);
    void insertBefore(Instruction *, Instruction *);
    void remove(Instruction *);
    bool empty() const { return head->getNext() == head; }
    void output() const;
    bool succEmpty() const { return succ.empty(); };
    bool predEmpty() const { return pred.empty(); };
    void addSucc(BasicBlock *);
    void removeSucc(BasicBlock *);
    void addPred(BasicBlock *);
    void removePred(BasicBlock *);
    int getNo() { return no; };
    Function *getParent() { return parent; };
    void setParent(Function *func) { parent = func; };
    Instruction *begin() { return head->getNext(); };
    Instruction *end() { return head; };
    Instruction *rbegin() { return head->getPrev(); };
    Instruction *rend() { return head; };
    bb_iterator succ_begin() { return succ.begin(); };
    bb_iterator succ_end() { return succ.end(); };
    bb_iterator pred_begin() { return pred.begin(); };
    bb_iterator pred_end() { return pred.end(); };
    int getNumOfPred() const { return pred.size(); };
    int getNumOfSucc() const { return succ.size(); };
    void genMachineCode(AsmBuilder *);
};

#endif