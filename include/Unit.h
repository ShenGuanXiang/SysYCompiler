#ifndef __UNIT_H__
#define __UNIT_H__

#include <vector>
#include "Function.h"
#include "AsmBuilder.h"
#include <unordered_set>

class Unit
{
    typedef std::vector<Function *>::iterator iterator;
    typedef std::vector<Function *>::reverse_iterator reverse_iterator;

private:
    std::vector<Function *> func_list;
    std::unordered_set<IdentifierSymbolEntry *> decl_list;
    std::unordered_set<IdentifierSymbolEntry *> decl_threadFunc_list;
    Function *main_func;

public:
    Unit() { main_func = nullptr; };
    ~Unit();
    void insertFunc(Function *);
    void insertDecl(IdentifierSymbolEntry *);
    void removeFunc(Function *);
    void removeDecl(IdentifierSymbolEntry *);
    void insertTFDecl(IdentifierSymbolEntry *se){ decl_threadFunc_list.insert(se);};
    void output() const;
    iterator begin() { return func_list.begin(); };
    iterator end() { return func_list.end(); };
    reverse_iterator rbegin() { return func_list.rbegin(); };
    reverse_iterator rend() { return func_list.rend(); };
    void genMachineCode(MachineUnit *munit);

    std::unordered_set<IdentifierSymbolEntry *> getDeclList() { return decl_list; };
    Function *getMainFunc();
    // DCE
    std::vector<Function *> getFuncList() { return func_list; };

    // Inline
    void getCallGraph();
};

#endif