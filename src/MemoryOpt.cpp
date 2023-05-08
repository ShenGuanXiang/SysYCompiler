#include "MemoryOpt.h"
#include <list>

void MemoryOpt::pass()
{
    for (auto func = unit->begin(); func != unit->end(); func++)
    {
        pass(*func);
    }
}

void MemoryOpt::pass(Function *func)
{
    // TODO：预处理：记录每个func store的地址有哪些 & 记录op对应的数组地址
    std::map<std::pair<IdentifierSymbolEntry *, int>, Operand *> arraddr2op;         // ((arr, offset), %t1) together with "getAddr()" for cse
    std::map<Operand *, std::pair<IdentifierSymbolEntry *, int>> op2arraddr;         // (%t1, (arr, offset))
    std::map<IdentifierSymbolEntry *, double> scalar2val;                            // (int/float var, const_val)
    std::map<IdentifierSymbolEntry *, std::vector<std::pair<bool, double>>> arr2val; // (arr, {(isconst, const_val)})
    if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() == "main")
    {
        for (auto id_se : unit->getDeclList())
        {
            if (id_se->getType()->isARRAY())
            {
                for (auto val : id_se->getArrVals())
                    arr2val[id_se].push_back({true, val});
            }
            else
            {
                assert(id_se->getType()->isInt() || id_se->getType()->isFloat());
                scalar2val[id_se] = id_se->getValue();
            }
        }
    }

    auto entry = func->getEntry();
    std::list<BasicBlock *> q;
    std::set<BasicBlock *> visited;

    q.push_back(entry);
    visited.insert(entry);
}

void MemoryOpt::visit(Instruction *inst)
{
    switch (inst->getInstType())
    {
    case Instruction::GEP:
    {
        break;
    }
    case Instruction::CALL:
    {
        break;
    }
    case Instruction::STORE:
    {
        if (inst->getUses()[1]->getType()->isConst())
        {
        }
        else
        {
        }
        break;
    }
    case Instruction::LOAD:
    {
        break;
    }
    default:
        break;
    }
}