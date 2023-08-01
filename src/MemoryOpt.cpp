#include "MemoryOpt.h"
#include <list>

static std::map<Function *, std::map<SymbolEntry *, Operand *>> changed_scalar;
static std::map<Function *, std::map<SymbolEntry *, std::vector<Operand *>>> changed_arr;

// TODO：删除无用写操作

struct Lattice
{
    std::map<std::pair<SymbolEntry *, int>, Operand *> arraddr2op; // ((arr, offset), %t1) (for cse)
    std::map<Operand *, std::pair<SymbolEntry *, int>> op2arraddr; // (%t1, (arr, offset)) offset == -1 means uncertain
    std::map<SymbolEntry *, Operand *> scalar2op;                  // (int/float var, op)
    std::map<SymbolEntry *, std::vector<Operand *>> arr2ops;       // (arr, {op}) op == nullptr means uncertain

    bool operator==(const Lattice &other) const
    {
        return arraddr2op == other.arraddr2op && op2arraddr == other.op2arraddr &&
               scalar2op == other.scalar2op && arr2ops == other.arr2ops;
    }
};

static std::map<BasicBlock *, Lattice> bb_in, bb_out;

static Lattice meet(Lattice a, Lattice b)
{
    Lattice result;
    for (auto element : a.arraddr2op)
    {
        if (b.arraddr2op.count(element.first) && b.arraddr2op[element.first] == element.second)
        {
            result.arraddr2op.insert(element);
        }
    }
    for (auto element : a.op2arraddr)
    {
        if (b.op2arraddr.count(element.first) && b.op2arraddr[element.first] == element.second)
        {
            result.op2arraddr.insert(element);
        }
    }
    for (auto element : a.scalar2op)
    {
        if (b.scalar2op.count(element.first) && b.scalar2op[element.first] == element.second)
        {
            result.scalar2op.insert(element);
        }
    }
    for (auto element : a.arr2ops)
    {
        if (b.arr2ops.count(element.first))
        {
            for (int i = 0; i < element.second.size(); i++)
            {
                if (b.arr2ops[element.first][i] != element.second[i])
                {
                    element.second[i] = nullptr;
                }
            }
            result.arr2ops.insert(element);
        }
    }
    return result;
}

static Lattice transfer(Lattice in, BasicBlock *bb, bool is_global)
{
    Lattice out;
    // TODO :同时做块内局部优化包括常量传播，删除无用读写指令
    for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
    {
        switch (inst->getInstType())
        {
        case Instruction::GEP:
        {
            SymbolEntry *se = inst->getUses()[0]->getEntry();
            int offset = 0;
            if (se != nullptr)
            {
                auto arrType = dynamic_cast<ArrayType *>(dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType());
                int cur_size = arrType->getSize() / arrType->getElemType()->getSize();
                auto dims = arrType->fetch();
                for (auto i = 1U, k = 0U; i != inst->getUses().size(); i++, k++)
                {
                    if (!inst->getUses()[i]->getType()->isConst())
                    {
                        out.op2arraddr[inst->getDef()] = std::pair<SymbolEntry *, int>{se, -1};
                        se = nullptr;
                        break;
                    }
                    offset += inst->getUses()[i]->getEntry()->getValue() * cur_size;
                    if (k != dims.size())
                        cur_size /= dims[k++];
                }
            }
            if (se != nullptr)
            {
                out.arraddr2op[std::pair<SymbolEntry *, int>{se, offset}] = inst->getDef();
                out.op2arraddr[inst->getDef()] = std::pair<SymbolEntry *, int>{se, offset};
            }
            break;
        }
        case Instruction::CALL:
        {
            auto callee = dynamic_cast<FuncCallInstruction *>(inst)->getFuncSe();
            if (changed_arr.count(callee->getFunction()))
            {
                for (auto kv : changed_arr[callee->getFunction()])
                {
                    out.arr2ops[kv.first] = kv.second;
                }
            }
            if (changed_scalar.count(callee->getFunction()))
            {
                for (auto kv : changed_scalar[callee->getFunction()])
                {
                    out.scalar2op[kv.first] = kv.second;
                }
            }
            if (callee->getName() == "memset")
            {
                assert(inst->getUses()[0]->getType()->isPTR() && dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType()->isARRAY());
                auto arrType = dynamic_cast<ArrayType *>(dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType());
                assert(arrType->getElemType()->getSize() == 4);
                assert(inst->getUses()[1]->getEntry()->getValue() == 0);
                auto se = inst->getUses()[0]->getEntry();
                if (!out.arr2ops.count(se))
                    for (int i = 0; i < arrType->getSize() / arrType->getElemType()->getSize(); i++)
                        out.arr2ops[se].push_back(nullptr);
                for (int i = 0; i < inst->getUses()[2]->getEntry()->getValue() / 4; i++)
                {
                    out.arr2ops[se][i] = new Operand(new ConstantSymbolEntry(inst->getUses()[1]->getType(), inst->getUses()[1]->getEntry()->getValue()));
                }
            }
            break;
        }
        case Instruction::STORE:
        {
            assert(inst->getUses()[0]->getType()->isPTR());
            if (out.op2arraddr.count(inst->getUses()[0]))
            {
                auto se = out.op2arraddr[inst->getUses()[0]].first;
                if (!out.arr2ops.count(se))
                {
                    int elemNum = dynamic_cast<PointerType *>(se->getType())->getValType()->getSize() / dynamic_cast<ArrayType *>(dynamic_cast<PointerType *>(se->getType())->getValType())->getElemType()->getSize();
                    for (int i = 0; i < elemNum; i++)
                        out.arr2ops[se].push_back(nullptr);
                }
                if (out.op2arraddr[inst->getUses()[0]].second == -1)
                {
                    for (int i = 0; i < out.arr2ops[se].size(); i++)
                    {
                        out.arr2ops[se][i] = nullptr;
                    }
                }
                else
                {
                    out.arr2ops[se][out.op2arraddr[inst->getUses()[0]].second] = inst->getUses()[1];
                }
            }
            else
            {
                out.scalar2op[dynamic_cast<IdentifierSymbolEntry *>(inst->getUses()[0]->getEntry())] = inst->getUses()[1];
            }
            break;
        }
        case Instruction::PHI:
        {
            // TODO
            break;
        }
        default:
            break;
        }
    }

    // TODO : out与in合并

    return out;
}

void MemoryOpt::pass()
{
    for (auto func = unit->begin(); func != unit->end(); func++)
    {
        pass(*func);
    }
}

void MemoryOpt::pass(Function *func)
{
    auto entry = func->getEntry();

    // TODO：做参数的数组分配一定空间？

    if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() == "main")
    {
        for (auto id_se : unit->getDeclList())
        {
            if (id_se->getType()->isARRAY())
            {
                for (auto val : id_se->getArrVals())
                    bb_in[entry].arr2ops[id_se].push_back(new Operand(new ConstantSymbolEntry(((ArrayType *)(id_se->getType()))->getElemType(), val)));
            }
            else
            {
                assert(id_se->getType()->isInt() || id_se->getType()->isFloat());
                bb_in[entry].scalar2op[id_se] = new Operand(new ConstantSymbolEntry(id_se->getType(), id_se->getValue()));
            }
        }
    }

    bool changed;
    do
    {
        changed = false;

        std::list<BasicBlock *> q;
        std::set<BasicBlock *> visited;

        q.push_back(entry);
        visited.insert(entry);

        std::map<SymbolEntry *, Operand *> cur_changed_scalar;
        std::map<SymbolEntry *, std::vector<Operand *>> cur_changed_arr;

        while (!q.empty())
        {
            auto cur_bb = q.front();
            q.pop_front();

            Lattice in = cur_bb->predEmpty() ? Lattice() : bb_out[*(cur_bb->pred_begin())];
            for (auto bb_iter = cur_bb->pred_begin(); bb_iter != cur_bb->pred_end(); bb_iter++)
            {
                in = meet(in, bb_out[*(bb_iter)]);
            }
            Lattice out = transfer(in, cur_bb, false);

            for (auto succ_iter = cur_bb->succ_begin(); succ_iter != cur_bb->succ_end(); succ_iter++)
            {
                if (!visited.count(*succ_iter))
                {
                    visited.insert(*succ_iter);
                    q.push_back(*succ_iter);
                }
            }

            if (!(in == bb_in[cur_bb] && out == bb_out[cur_bb]))
            {
                changed = true;
                bb_in[cur_bb] = in;
                bb_out[cur_bb] = out;
            }

            if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() != "main" && cur_bb->succEmpty())
            {
                for (auto kv : out.scalar2op)
                {
                    if (cur_changed_scalar.count(kv.first) && cur_changed_scalar[kv.first] != kv.second)
                        cur_changed_scalar[kv.first] = nullptr;
                    else
                        cur_changed_scalar[kv.first] = kv.second;
                }
                for (auto kv : out.arr2ops)
                {
                    if (cur_changed_arr.count(kv.first))
                    {
                        for (int i = 0; i < cur_changed_arr[kv.first].size(); i++)
                        {
                            if (cur_changed_arr[kv.first][i] != kv.second[i])
                                cur_changed_arr[kv.first][i] = nullptr;
                            else
                                cur_changed_arr[kv.first][i] = kv.second[i];
                        }
                    }
                    else
                        cur_changed_arr[kv.first] = kv.second;
                }
            }
        }

        if (!changed_arr.count(func) || cur_changed_arr != changed_arr[func] || !changed_scalar.count(func) || cur_changed_scalar != changed_scalar[func])
        {
            changed = true;
            changed_arr[func] = cur_changed_arr;
            changed_scalar[func] = cur_changed_scalar;
        }

        func->SimplifyPHI();

    } while (changed);

    for (auto bb : func->getBlockList())
        transfer(bb_in[bb], bb, true);
}