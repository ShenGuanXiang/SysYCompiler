#ifndef __FUNCTION_H__
#define __FUNCTION_H__

#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <iostream>
#include "BasicBlock.h"
#include "SymbolTable.h"
#include "AsmBuilder.h"

class Unit;

class Function
{
    typedef std::vector<BasicBlock *>::iterator iterator;
    typedef std::vector<BasicBlock *>::reverse_iterator reverse_iterator;

private:
    std::vector<BasicBlock *> block_list;
    SymbolEntry *sym_ptr;
    BasicBlock *entry;
    Unit *parent;
    std::vector<SymbolEntry *> param_list;

    std::set<Function *> callers;
    std::set<Function *> callees;
    std::set<Instruction *> callers_instr;
    int degree = 0;

    // DCE
    int iscritical = -1;

public:
    Function(Unit *, SymbolEntry *);
    ~Function();
    void insertBlock(BasicBlock *bb) { block_list.push_back(bb); };
    BasicBlock *getEntry() { return entry; };
    void setEntry(BasicBlock *bb) { entry = bb; };
    Unit *getParent() { return parent; };
    void setParent(Unit *unit) { parent = unit; };
    void remove(BasicBlock *bb);
    void output() const;
    std::vector<BasicBlock *> &getBlockList() { return block_list; };
    std::vector<SymbolEntry *> &getParamsList() { return param_list; };
    std::vector<Operand *> getParamsOp()
    {
        std::vector<Operand *> paramOps;
        for (auto id_se : param_list)
        {
            paramOps.push_back(dynamic_cast<IdentifierSymbolEntry *>(id_se)->getParamOpe());
        }
        return paramOps;
    };
    iterator begin() { return block_list.begin(); };
    iterator end() { return block_list.end(); };
    reverse_iterator rbegin() { return block_list.rbegin(); };
    reverse_iterator rend() { return block_list.rend(); };
    SymbolEntry *getSymPtr() { return sym_ptr; };
    void insertParams(SymbolEntry *param)
    {
        param_list.push_back(param);
        dynamic_cast<IdentifierSymbolEntry *>(sym_ptr)->getParamsSe().insert(dynamic_cast<IdentifierSymbolEntry *>(param));
    }
    void ComputeDom();
    void ComputeDomFrontier();
    void genMachineCode(AsmBuilder *);

    void SimplifyPHI();

    // DCE
    bool ever_called;
    void ComputeRDom();
    void ComputeRiDom();
    void ComputeRDF();
    bool isCritical();
    void ClearCalled() { ever_called = false; };
    void SetCalled() { ever_called = true; };
    bool isCalled() { return ever_called; };
    BasicBlock *get_nearest_dom(Instruction *instr);
    std::set<BasicBlock *> getExits();

    // AutoInline
    bool isrecur = false;
    int cal_inst_num;
    int isCalc() { return cal_inst_num; };
    void SetCalcInstNum(int num) { cal_inst_num = num; };
    bool isRecur() { return isrecur; };
    void SetRecur() { isrecur = true; };
    std::set<Function *> &getCallers() { return callers; };
    std::set<Instruction *> &getCallers_instr() { return callers_instr; };
    std::set<Function *> &getCallees() { return callees; };
    int &getdegree() { return degree; };
};

#endif
