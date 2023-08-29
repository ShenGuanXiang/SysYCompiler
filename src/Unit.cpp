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
    if (std::find(func_list.begin(), func_list.end(), func) != func_list.end())
        func_list.erase(std::find(func_list.begin(), func_list.end(), func));
    func->setParent(nullptr);
}

void Unit::removeDecl(IdentifierSymbolEntry *se)
{
    decl_list.erase(se);
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
    {
        if (!decl->isLibFunc() && !(decl->getAddr()->usersNum() == 0 && decl->getAddr()->defsNum() == 0))
        {
            if (!decl->getType()->isConst() || decl->getType()->isARRAY())
                munit->insertGlobalVar(decl);
        }
        else if (decl->isLibFunc() && (decl->getName() == "__mulmod" || decl->getName() == "__create_threads" || decl->getName() == "__join_threads" || decl->getName() == "__bind_core" || decl->getName() == "__lock" || decl->getName() == "__unlock" || decl->getName() == "__barrier"))
        {
            munit->insertGlobalVar(decl);
        }
    }
    for (auto &func : func_list)
        func->genMachineCode(builder);
    delete builder;
}

Function *Unit::getMainFunc()
{
    if (main_func != nullptr)
        return main_func;
    for (auto f : getFuncList())
    {
        if (((IdentifierSymbolEntry *)f->getSymPtr())->getName() == "main")
        {
            main_func = f;
            return main_func;
        }
    }
    assert(0 && "no main func?");
}

void Unit::check()
{
    for (auto func : func_list)
    {
        for (auto bb : func->getBlockList())
        {
            for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
            {
                if (!inst->hasNoDef())
                {
                    assert(inst->getDef()->getDef() == inst);
                }
                for (auto use : inst->getUses())
                {
                    assert(use->getUses().count(inst));
                }
            }
        }
    }
}

Unit::~Unit()
{
    auto delete_list = func_list;
    for (auto &func : delete_list)
        delete func;
}
