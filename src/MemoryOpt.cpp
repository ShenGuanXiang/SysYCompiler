#include "MemoryOpt.h"
#include <list>

static std::map<Function *, std::map<SymbolEntry *, Operand *>> changed_scalar;
static std::map<Function *, std::map<SymbolEntry *, std::map<int, Operand *>>> changed_arr;

// TODO：如果写操作不会对之后的读操作产生影响，则删除写操作
// TODO：对于数组没有被修改过的情况，将对数组的固定下标的访问替换为数组全局初始化的值

struct Lattice
{
    std::map<std::pair<SymbolEntry *, int>, Operand *> arraddr2op; // ((arr, offset), %t1) (for cse)
    std::map<Operand *, std::pair<SymbolEntry *, int>> op2arraddr; // (%t1, (arr, offset)) offset == -1 means uncertain
    std::map<SymbolEntry *, Operand *> scalar2op;                  // (int/float var, op)
    std::map<SymbolEntry *, std::map<int, Operand *>> arr2ops;     // (arr, {op}) op == nullptr means uncertain

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
            for (auto kv : element.second)
            {
                int i = kv.first;
                if (b.arr2ops[element.first].count(i) && b.arr2ops[element.first][i] == kv.second)
                {
                    result.arr2ops[element.first][i] = kv.second;
                }
            }
        }
    }
    return result;
}

static Lattice transfer(Lattice in, BasicBlock *bb, bool is_global)
{
    Lattice out;
    if (is_global)
        out = in;
    std::set<Instruction *> freeInsts;
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
                auto arrType = dynamic_cast<ArrayType *>(dynamic_cast<PointerType *>(se->getType())->getValType());
                int cur_size = arrType->getSize() / arrType->getElemType()->getSize();
                auto dims = arrType->fetch();
                auto k = 0U;
                for (auto i = 1U; i != inst->getUses().size(); i++)
                {
                    if (!inst->getUses()[i]->getType()->isConst())
                    {
                        out.op2arraddr[inst->getDef()] = std::pair<SymbolEntry *, int>{se, -1};
                        se = nullptr;
                        break;
                    }
                    offset += inst->getUses()[i]->getEntry()->getValue() * cur_size;
                    if (k < dims.size())
                        cur_size /= dims[k++];
                }
            }
            if (se != nullptr)
            {
                if (out.arraddr2op.count(std::pair<SymbolEntry *, int>{se, offset})) // the same address appeared somewhere before
                {
                    inst->replaceAllUsesWith(out.arraddr2op[std::pair<SymbolEntry *, int>{se, offset}]);
                    freeInsts.insert(inst);
                }
                else
                {
                    out.arraddr2op[std::pair<SymbolEntry *, int>{se, offset}] = inst->getDef();
                    out.op2arraddr[inst->getDef()] = std::pair<SymbolEntry *, int>{se, offset};
                }
            }
            break;
        }
        case Instruction::CALL:
        {
            auto callee = dynamic_cast<FuncCallInstruction *>(inst)->getFuncSe();
            if (callee->getFunction() != nullptr && changed_arr.count(callee->getFunction()))
            {
                for (auto kv : changed_arr[callee->getFunction()])
                {
                    for (auto kv1 : kv.second)
                        out.arr2ops[kv.first][kv1.first] = kv1.second;
                }
            }
            if (callee->getFunction() != nullptr && changed_scalar.count(callee->getFunction()))
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
                for (int i = 0; i < inst->getUses()[2]->getEntry()->getValue() / 4; i++)
                {
                    if (bb_out[bb].arr2ops.count(se) && bb_out[bb].arr2ops[se].count(i) && bb_out[bb].arr2ops[se][i] != nullptr && bb_out[bb].arr2ops[se][i]->getType()->isConst() && bb_out[bb].arr2ops[se][i]->getEntry()->getValue() == inst->getUses()[1]->getEntry()->getValue())
                        out.arr2ops[se][i] = bb_out[bb].arr2ops[se][i];
                    else
                        out.arr2ops[se][i] = new Operand(new ConstantSymbolEntry(inst->getUses()[1]->getType(), inst->getUses()[1]->getEntry()->getValue())); // 可能导致内存泄漏
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
                if (out.op2arraddr[inst->getUses()[0]].second == -1)
                {
                    auto arrType = dynamic_cast<ArrayType *>(dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType());
                    for (int i = 0; i < arrType->getSize() / arrType->getElemType()->getSize(); i++)
                    {
                        out.arr2ops[se][i] = nullptr;
                    }
                }
                else
                {
                    if (out.arr2ops.count(se) && out.arr2ops[se].count(out.op2arraddr[inst->getUses()[0]].second))
                    {
                        auto old = out.arr2ops[se][out.op2arraddr[inst->getUses()[0]].second];
                        if (old == inst->getUses()[1] || (old->getType()->isConst() && inst->getUses()[1]->getType()->isConst() && old->getEntry()->getValue() == inst->getUses()[1]->getEntry()->getValue()))
                            freeInsts.insert(inst);
                        else
                            out.arr2ops[se][out.op2arraddr[inst->getUses()[0]].second] = inst->getUses()[1];
                    }
                    else
                        out.arr2ops[se][out.op2arraddr[inst->getUses()[0]].second] = inst->getUses()[1];
                }
            }
            else
            {
                if (out.scalar2op.count(dynamic_cast<IdentifierSymbolEntry *>(inst->getUses()[0]->getEntry())))
                {
                    auto old = out.scalar2op[dynamic_cast<IdentifierSymbolEntry *>(inst->getUses()[0]->getEntry())];
                    if (old == inst->getUses()[1] || (old->getType()->isConst() && inst->getUses()[1]->getType()->isConst() && old->getEntry()->getValue() == inst->getUses()[1]->getEntry()->getValue()))
                        freeInsts.insert(inst);
                    else
                        out.scalar2op[dynamic_cast<IdentifierSymbolEntry *>(inst->getUses()[0]->getEntry())] = inst->getUses()[1];
                }
                else
                    out.scalar2op[dynamic_cast<IdentifierSymbolEntry *>(inst->getUses()[0]->getEntry())] = inst->getUses()[1];
            }
            break;
        }
        case Instruction::LOAD:
        {
            if (out.op2arraddr.count(inst->getUses()[0]))
            {
                auto [se, offset] = out.op2arraddr[inst->getUses()[0]];
                assert(se != nullptr);
                if (out.arr2ops.count(se) && out.arr2ops[se].count(offset) && out.arr2ops[se][offset] != nullptr)
                {
                    inst->replaceAllUsesWith(out.arr2ops[se][offset]);
                    freeInsts.insert(inst);
                }
            }
            else if (out.scalar2op.count(inst->getUses()[0]->getEntry()))
            {
                assert(out.scalar2op[inst->getUses()[0]->getEntry()] != nullptr);
                inst->replaceAllUsesWith(out.scalar2op[inst->getUses()[0]->getEntry()]);
                freeInsts.insert(inst);
            }
            break;
        }
        case Instruction::PHI:
        {
            // TODO
            break;
        }
        case Instruction::BINARY:
        {
            if (inst->getUses()[0]->getType()->isConst() && inst->getUses()[1]->getType()->isConst())
            {
                auto val1 = inst->getUses()[0]->getEntry()->getValue(), val2 = inst->getUses()[0]->getEntry()->getValue();
                switch (dynamic_cast<BinaryInstruction *>(inst)->getOpcode())
                {
                case BinaryInstruction::ADD:
                    inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), val1 + val2)));
                    break;
                case BinaryInstruction::SUB:
                    inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), val1 - val2)));
                    break;
                case BinaryInstruction::MUL:
                    inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), val1 * val2)));
                    break;
                case BinaryInstruction::DIV:
                    inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), val1 / val2)));
                    break;
                case BinaryInstruction::MOD:
                    inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), (int)val1 % (int)val2)));
                    break;
                default:
                    assert(0 && "unimplemented binary inst type");
                }
                freeInsts.insert(inst);
            }
            break;
        }
        case Instruction::IFCAST:
        {

            break;
        }
        default:
            break;
        }
    }

    if (!is_global)
    {
        // out与in合并
        for (auto element : in.arraddr2op)
        {
            if (!out.arraddr2op.count(element.first))
            {
                out.arraddr2op.insert(element);
            }
        }
        for (auto element : in.op2arraddr)
        {
            if (!out.op2arraddr.count(element.first))
            {
                out.op2arraddr.insert(element);
            }
        }
        for (auto element : in.scalar2op)
        {
            if (!out.scalar2op.count(element.first))
            {
                out.scalar2op.insert(element);
            }
        }
        for (auto element : in.arr2ops)
        {
            for (auto kv : element.second)
            {
                int i = kv.first;
                if (out.arr2ops.count(element.first) && out.arr2ops[element.first].count(i))
                    ;
                else
                {
                    out.arr2ops[element.first][i] = kv.second;
                }
            }
        }
        for (auto element : out.arr2ops)
        {
            for (auto kv : element.second)
            {
                if (kv.second == nullptr)
                    out.arr2ops[element.first].erase(kv.first);
            }
        }
        for (auto inst : freeInsts)
        {
            delete inst;
        }
    }

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

    bool changed;
    do
    {
        changed = false;

        std::list<BasicBlock *> q;
        std::set<BasicBlock *> visited;

        q.push_back(entry);
        visited.insert(entry);

        std::map<SymbolEntry *, Operand *> cur_changed_scalar;
        std::map<SymbolEntry *, std::map<int, Operand *>> cur_changed_arr;

        while (!q.empty())
        {
            auto cur_bb = q.front();
            q.pop_front();

            Lattice in;
            bool first = true;
            if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() == "main" && cur_bb == entry)
            {
                for (auto id_se : unit->getDeclList())
                {
                    if (id_se->getType()->isARRAY())
                    {
                        int i = 0;
                        for (auto val : id_se->getArrVals())
                            in.arr2ops[id_se][i++] = new Operand(new ConstantSymbolEntry(((ArrayType *)(id_se->getType()))->getElemType(), val));
                    }
                    else if (id_se->getType()->isInt() || id_se->getType()->isFloat())
                    {
                        in.scalar2op[id_se] = new Operand(new ConstantSymbolEntry(id_se->getType(), id_se->getValue()));
                    }
                }
                first = false;
            }
            for (auto bb_iter = cur_bb->pred_begin(); bb_iter != cur_bb->pred_end(); bb_iter++)
            {
                if (bb_out.count(*bb_iter))
                {
                    if (first)
                    {
                        in = bb_out[*(bb_iter)];
                        first = false;
                    }
                    else
                        in = meet(in, bb_out[*(bb_iter)]);
                }
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

            if (!bb_in.count(cur_bb) || !bb_out.count(cur_bb) || !(in == bb_in[cur_bb] && out == bb_out[cur_bb]))
            {
                // if (bb_out.count(cur_bb))
                // {
                //     fprintf(stderr, "%d%d%d%d\n", bb_out[cur_bb].arraddr2op == out.arraddr2op, bb_out[cur_bb].arr2ops == out.arr2ops, bb_out[cur_bb].op2arraddr == out.op2arraddr, bb_out[cur_bb].scalar2op == out.scalar2op);
                // }
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
                        for (auto kv1 : kv.second)
                        {
                            int i = kv1.first;
                            if (cur_changed_arr[kv.first].count(i) && cur_changed_arr[kv.first][i] != kv.second[i])
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

        if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() != "main")
        {
            if (!changed_arr.count(func) || cur_changed_arr != changed_arr[func] || !changed_scalar.count(func) || cur_changed_scalar != changed_scalar[func])
            {
                changed = true;
                changed_arr[func] = cur_changed_arr;
                changed_scalar[func] = cur_changed_scalar;
            }
        }

        func->SimplifyPHI();

    } while (changed);

    for (auto bb : func->getBlockList())
        transfer(bb_in[bb], bb, true);
}