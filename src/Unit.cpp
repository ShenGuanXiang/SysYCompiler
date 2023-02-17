#include "Unit.h"

void Unit::insertFunc(Function *f)
{
    func_list.push_back(f);
}

void Unit::insertDecl(IdentifierSymbolEntry *se)
{
    decl_list.insert(se);
}

void Unit::removeFunc(Function *func)
{
    func_list.erase(std::find(func_list.begin(), func_list.end(), func));
}

void Unit::output() const
{
    for (auto item : decl_list)
        if (!item->isLibFunc())
            item->decl_code();
    for (auto item : decl_list)
        if (item->isLibFunc())
            item->decl_code();
    for (auto func : func_list)
        func->output();
}

void Unit::genMachineCode(MachineUnit *munit)
{
    AsmBuilder *builder = new AsmBuilder();
    builder->setUnit(munit);
    for (auto decl : decl_list)
        if ((!decl->isLibFunc() && !decl->getType()->isConst()) || decl->getType()->isARRAY()) // TODO：如果数组实现了常量折叠就不用加了
            munit->insertGlobalVar(decl);
    for (auto &func : func_list)
        func->genMachineCode(builder);
    delete builder;
}

Unit::~Unit()
{
    auto delete_list = func_list;
    for (auto &func : delete_list)
        delete func;
}
