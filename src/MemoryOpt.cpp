#include "MemoryOpt.h"
#include <list>

static std::map<Function *, std::map<IdentifierSymbolEntry *, Operand *>> changed_scalar;
static std::map<Function *, std::map<IdentifierSymbolEntry *, std::vector<Operand *>>> changed_arr;

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
    std::map<std::pair<IdentifierSymbolEntry *, int>, Operand *> arraddr2op; // ((arr, offset), %t1) together with "getAddr()" for cse
    std::map<Operand *, std::pair<IdentifierSymbolEntry *, int>> op2arraddr; // (%t1, (arr, offset))
    std::map<IdentifierSymbolEntry *, Operand *> scalar2op;                  // (int/float var, op) op == nullptr means uncertain
    std::map<IdentifierSymbolEntry *, std::vector<Operand *>> arr2op;        // (arr, {op})
    if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() == "main")
    {
        for (auto id_se : unit->getDeclList())
        {
            if (id_se->getType()->isARRAY())
            {
                for (auto val : id_se->getArrVals())
                    arr2op[id_se].push_back(new Operand(new ConstantSymbolEntry(((ArrayType *)(id_se->getType()))->getElemType(), val)));
            }
            else
            {
                assert(id_se->getType()->isInt() || id_se->getType()->isFloat());
                scalar2op[id_se] = new Operand(new ConstantSymbolEntry(id_se->getType(), id_se->getValue()));
            }
        }
    }

    // TODO：内联后传参用的Gep可以合并到被内联函数体的定义中

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