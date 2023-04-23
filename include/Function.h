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

    // DCE
    std::map<Function *, std::set<Instruction *>> preds_instr;
    std::set<BasicBlock *> Exit;
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
    std::map<Function *, std::set<Instruction *>> getPreds() { return preds_instr; };
    BasicBlock *get_nearest_dom(Instruction *instr);
    std::set<BasicBlock *> &getExit();
    void removePred(Instruction *instr);

    // AutoInline
    bool isrecur = false;
    int cal_inst_num;
    int isCalc() { return cal_inst_num; };
    void SetCalcInstNum(int num) { cal_inst_num = num; };
    bool isRecur() { return isrecur; };
    void SetRecur() { isrecur = true; }
};

#endif
