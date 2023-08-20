#include "MemoryOpt.h"
#include "SimplifyCFG.h"
#include <list>
#include <stack>

// TODO：反向做一遍，删除没用的Store，删除无用的memset
// 遇到很大的局部数组会超时

struct Info
{
    std::map<SymbolEntry *, Operand *> scalar2op;              // (int/float var, op)
    std::map<SymbolEntry *, std::map<int, Operand *>> arr2ops; // (arr, {op})
    std::set<SymbolEntry *> unknown_arr;                       // clear the whole array TODO:加一个起始偏移，数组部分未知
    std::set<SymbolEntry *> unknown_scalar;

    bool operator==(const Info &other) const
    {
        return scalar2op == other.scalar2op && arr2ops == other.arr2ops;
    }

    void print()
    {
        fprintf(stderr, "scalar2op:\n");
        for (auto [se, op] : scalar2op)
        {
            fprintf(stderr, "%s, %s\n", se->toStr().c_str(), op == nullptr ? "nullptr" : op->toStr().c_str());
        }
        fprintf(stderr, "arr2ops:\n");
        for (auto [se, mp] : arr2ops)
        {
            for (auto [idx, op] : mp)
                fprintf(stderr, "%s, (%d, %s)\n", se->toStr().c_str(), idx, op == nullptr ? "nullptr" : op->toStr().c_str());
        }
        fprintf(stderr, "unknown_arr:\n");
        for (auto se : unknown_arr)
        {
            fprintf(stderr, "%s\n", se->toStr().c_str());
        }
        fprintf(stderr, "unknown_scalar:\n");
        for (auto se : unknown_scalar)
        {
            fprintf(stderr, "%s\n", se->toStr().c_str());
        }
    }
};

static std::map<BasicBlock *, Info> bb_in, bb_out;
static std::map<Function *, Info> func_out;

static Info meet(Info a, Info b)
{
    Info result;

    for (auto [se, op] : a.scalar2op)
    {
        if (b.scalar2op.count(se))
        {
            if (b.scalar2op[se] == op || (b.scalar2op[se] != nullptr && b.scalar2op[se]->getType()->isConst() && op != nullptr && op->getType()->isConst() && b.scalar2op[se]->getEntry()->getValue() == op->getEntry()->getValue()))
            {
                result.scalar2op[se] = op;
            }
            else
            {
                result.scalar2op[se] = nullptr;
            }
        }
        else
        {
            if (!b.unknown_scalar.count(se))
            {
                result.scalar2op[se] = nullptr;
            }
        }
    }
    for (auto [se, op] : b.scalar2op)
    {
        if (!a.scalar2op.count(se) && !a.unknown_scalar.count(se))
        {
            result.scalar2op[se] = op;
        }
    }
    std::set_union(a.unknown_scalar.begin(), a.unknown_scalar.end(), b.unknown_scalar.begin(), b.unknown_scalar.end(), inserter(result.unknown_scalar, result.unknown_scalar.end()));

    for (auto [se, idx2op] : a.arr2ops)
    {
        for (auto [i, op] : idx2op)
        {
            if (b.arr2ops.count(se) && b.arr2ops[se].count(i))
            {
                if (b.arr2ops[se][i] == op || (b.arr2ops[se][i] != nullptr && b.arr2ops[se][i]->getType()->isConst() && op != nullptr && op->getType()->isConst() && b.arr2ops[se][i]->getEntry()->getValue() == op->getEntry()->getValue()))
                {
                    result.arr2ops[se][i] = op;
                }
                else
                {
                    result.arr2ops[se][i] = nullptr;
                }
            }
            else
            {
                if (!b.unknown_arr.count(se))
                {
                    result.arr2ops[se][i] = op;
                }
            }
        }
    }
    for (auto [se, idx2op] : b.arr2ops)
    {
        for (auto [i, op] : idx2op)
        {
            if (a.arr2ops.count(se) && a.arr2ops[se].count(i))
                ;
            else
            {
                if (!a.unknown_arr.count(se))
                {
                    result.arr2ops[se][i] = op;
                }
            }
        }
    }

    std::set_union(a.unknown_arr.begin(), a.unknown_arr.end(), b.unknown_arr.begin(), b.unknown_arr.end(), inserter(result.unknown_arr, result.unknown_arr.end()));

    return result;
}

// return: {se, offset}
std::pair<SymbolEntry *, int> analyzeGep(Instruction *inst)
{
    assert(inst->isGep());
    SymbolEntry *se = inst->getUses()[0]->getEntry();
    assert(se != nullptr && se->getType()->isPTR());

    int offset = 0;
    int cur_size;
    std::vector<int> dims;
    auto k = 0U;

    // 未内联且未mem2reg的形参数组（用二重指针指向数组地址） 或者 gvnpre产生的phi i32*
    if (inst->getUses()[0]->getDef() && (inst->getUses()[0]->getDef()->isPHI() || inst->getUses()[0]->getDef()->isLoad()))
    {
        return {nullptr, -1}; // 不处理了，直接停
    }

    // 内联后的实参数组
    else if (inst->getUses()[0]->getDef() && inst->getUses()[0]->getDef()->isGep())
    {
        auto origin_inst = inst;
        inst = inst->getUses()[0]->getDef();
        std::stack<Instruction *> st;
        while (inst->getUses()[0]->getDef() && inst->getUses()[0]->getDef()->isGep())
        {
            st.push(inst);
            inst = inst->getUses()[0]->getDef();
        }

        auto kv = analyzeGep(inst);
        se = kv.first;
        offset = kv.second;

        if (se == nullptr)
            return {nullptr, -1};
        else if (offset == -1)
            return {se, -1};
        else
        {
            assert(se->getType()->isPTR());
            if (dynamic_cast<PointerType *>(se->getType())->getValType()->isARRAY())
            {
                auto arrType = dynamic_cast<ArrayType *>(dynamic_cast<PointerType *>(se->getType())->getValType());
                cur_size = arrType->getSize() / arrType->getElemType()->getSize();
                dims = arrType->fetch();

                int delta_k = 0;
                for (int i = 1; i < inst->getUses().size(); i++)
                {
                    if (k < dims.size())
                    {
                        cur_size /= dims[k++];
                        delta_k++;
                    }
                }
                if (delta_k == inst->getUses().size() - 1)
                    cur_size *= dims[--k];
            }
            else
                cur_size = 1;

            while (!st.empty())
            {
                inst = st.top();
                st.pop();
                int delta_k = 0;
                for (auto i = 1U; i != inst->getUses().size(); i++)
                {
                    if (!inst->getUses()[i]->getType()->isConst())
                    {
                        return std::pair<SymbolEntry *, int>{se, -1};
                    }
                    offset += inst->getUses()[i]->getEntry()->getValue() * cur_size;
                    if (k < dims.size())
                    {
                        cur_size /= dims[k++];
                        delta_k++;
                    }
                }
                if (delta_k == inst->getUses().size() - 1)
                    cur_size *= dims[--k];
            }

            inst = origin_inst;
        }
    }

    // 全局/局部数组 或未内联且已mem2reg的二维及以上的形参数组
    else if (dynamic_cast<PointerType *>(se->getType())->getValType()->isARRAY())
    {
        auto arrType = dynamic_cast<ArrayType *>(dynamic_cast<PointerType *>(se->getType())->getValType());
        cur_size = arrType->getSize() / arrType->getElemType()->getSize();
        dims = arrType->fetch();
    }

    // 未内联且已mem2reg的一维形参数组
    else
    {
        assert(inst->getUses()[0]->getDef() == nullptr);
        assert(se->isVariable() && dynamic_cast<IdentifierSymbolEntry *>(se)->isParam());
        cur_size = 1;
    }

    for (auto i = 1U; i != inst->getUses().size(); i++)
    {
        if (!inst->getUses()[i]->getType()->isConst())
        {
            return std::pair<SymbolEntry *, int>{se, -1};
        }
        offset += inst->getUses()[i]->getEntry()->getValue() * cur_size;
        if (k < dims.size())
            cur_size /= dims[k++];
    }

    return std::pair<SymbolEntry *, int>{se, offset};
}

static std::pair<Info, bool> transfer(Info in, BasicBlock *bb, bool is_global, bool is_pre = false)
{
    Info out;

    if (is_global)
        out = in;

    std::set<Instruction *> freeInsts;

    for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
    {
        switch (inst->getInstType())
        {
        case Instruction::CALL:
        {
            auto callee = dynamic_cast<FuncCallInstruction *>(inst)->getFuncSe();
            if (callee->getName() == "memset")
            {
                assert(inst->getUses()[0]->getType()->isPTR() && dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType()->isARRAY());
                auto arrType = dynamic_cast<ArrayType *>(dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType());
                assert(arrType->getElemType()->getSize() == 4);
                assert(inst->getUses()[1]->getEntry()->getValue() == 0);
                auto se = inst->getUses()[0]->getEntry();
                if (inst->getUses()[2]->getEntry()->getValue() > 1000000)
                    return std::pair<Info, bool>{out, false};
                for (int i = 0; i < inst->getUses()[2]->getEntry()->getValue() / 4; i++)
                {
                    if (bb_out[bb].arr2ops.count(se) && bb_out[bb].arr2ops[se].count(i) && bb_out[bb].arr2ops[se][i] != nullptr && bb_out[bb].arr2ops[se][i]->getType() == Var2Const(inst->getUses()[1]->getType()) && bb_out[bb].arr2ops[se][i]->getEntry()->getValue() == inst->getUses()[1]->getEntry()->getValue())
                        out.arr2ops[se][i] = bb_out[bb].arr2ops[se][i];
                    else
                        out.arr2ops[se][i] = new Operand(new ConstantSymbolEntry(inst->getUses()[1]->getType(), inst->getUses()[1]->getEntry()->getValue()));
                }
            }
            else if (callee->getName() == "getarray" || callee->getName() == "getfarray")
            {
                auto param_op = inst->getUses()[0];
                if (param_op->getDef() == nullptr) // 如：int f(int a[][2]) { getarray(a);};
                {
                    assert(param_op->getEntry()->isVariable() && dynamic_cast<IdentifierSymbolEntry *>(param_op->getEntry())->isParam());
                    in.arr2ops.erase(param_op->getEntry());
                    out.arr2ops.erase(param_op->getEntry());
                    out.unknown_arr.insert(param_op->getEntry());
                    continue;
                }
                assert(param_op->getDef()->isGep() || param_op->getDef()->isPHI());
                int offset = 0;
                SymbolEntry *origin_se;
                if (param_op->getDef()->isGep())
                {
                    auto res = analyzeGep(param_op->getDef());
                    origin_se = res.first;
                    offset = res.second;
                    if (origin_se == nullptr)
                        return std::pair<Info, bool>{out, false};
                }
                else
                {
                    return std::pair<Info, bool>{out, false};
                }
                assert(origin_se->getType()->isPTR() && dynamic_cast<PointerType *>(origin_se->getType())->getValType()->isARRAY());
                if (in.arr2ops.count(origin_se))
                {
                    std::set<int> to_rm;
                    for (auto [idx, op] : in.arr2ops[origin_se])
                    {
                        if (idx >= offset)
                            to_rm.insert(idx);
                    }
                    for (auto idx : to_rm)
                        in.arr2ops[origin_se].erase(idx);
                }
                if (out.arr2ops.count(origin_se))
                {
                    std::set<int> to_rm;
                    for (auto [idx, op] : out.arr2ops[origin_se])
                    {
                        if (idx >= offset)
                            to_rm.insert(idx);
                    }
                    for (auto idx : to_rm)
                        out.arr2ops[origin_se].erase(idx);
                }
                out.unknown_arr.insert(origin_se);
            }
            else if (callee->getFunction() != nullptr && !callee->isLibFunc())
            {
                for (auto se : func_out[callee->getFunction()].unknown_arr)
                {
                    assert(se->isVariable());
                    auto id_se = dynamic_cast<IdentifierSymbolEntry *>(se);
                    assert(id_se->isGlobal() || id_se->isParam());
                    auto origin_se = se;
                    if (id_se->isParam())
                    {
                        auto param_op = inst->getUses()[id_se->getParamNo()];
                        if (param_op->getDef() == nullptr)
                        {
                            assert(param_op->getEntry()->isVariable() && dynamic_cast<IdentifierSymbolEntry *>(param_op->getEntry())->isParam());
                            origin_se = param_op->getEntry();
                        }
                        else
                        {
                            assert(param_op->getDef() && (param_op->getDef()->isGep() || param_op->getDef()->isPHI()));
                            if (param_op->getDef()->isGep())
                            {
                                origin_se = analyzeGep(param_op->getDef()).first;
                                if (origin_se == nullptr)
                                    return std::pair<Info, bool>{out, false};
                            }
                            else
                            {
                                return std::pair<Info, bool>{out, false};
                            }
                        }
                    }
                    in.arr2ops.erase(origin_se);
                    out.arr2ops.erase(origin_se);
                    out.unknown_arr.insert(origin_se);
                }
                for (auto se : func_out[callee->getFunction()].unknown_scalar)
                {
                    in.scalar2op.erase(se);
                    out.scalar2op.erase(se);
                    out.unknown_scalar.insert(se);
                }
            }
            break;
        }
        case Instruction::STORE:
        {
            auto dst_addr = inst->getUses()[0];
            assert(dst_addr->getType()->isPTR());
            assert(dst_addr->getEntry()->isVariable() || dst_addr->getDef());
            if (dst_addr->getEntry()->isVariable() || dst_addr->getDef()->isAlloca())
            {
                if (out.scalar2op.count(dst_addr->getEntry()))
                {
                    auto old_src = out.scalar2op[dst_addr->getEntry()];
                    if (old_src == inst->getUses()[1] || (old_src != nullptr && old_src->getType()->isConst() && inst->getUses()[1]->getType()->isConst() && old_src->getEntry()->getValue() == inst->getUses()[1]->getEntry()->getValue()))
                        freeInsts.insert(inst);
                    else
                        out.scalar2op[dst_addr->getEntry()] = inst->getUses()[1];
                }
                else
                    out.scalar2op[dst_addr->getEntry()] = inst->getUses()[1];
            }
            else if (dst_addr->getDef()->isGep())
            {
                auto [se, offset] = analyzeGep(dst_addr->getDef());
                if (se != nullptr)
                {
                    if (offset == -1)
                    {
                        // TODO：针对多次GEP的情况还可以更精细
                        in.arr2ops.erase(se);
                        out.arr2ops.erase(se);
                        out.unknown_arr.insert(se);
                    }
                    else
                    {
                        if (out.arr2ops.count(se) && out.arr2ops[se].count(offset))
                        {
                            auto old_src = out.arr2ops[se][offset];
                            if (old_src != nullptr && (old_src == inst->getUses()[1] || (old_src->getType()->isConst() && inst->getUses()[1]->getType()->isConst() && old_src->getEntry()->getValue() == inst->getUses()[1]->getEntry()->getValue())))
                                freeInsts.insert(inst);
                            else
                                out.arr2ops[se][offset] = inst->getUses()[1];
                        }
                        else
                            out.arr2ops[se][offset] = inst->getUses()[1];
                    }
                }
                else
                {
                    return std::pair<Info, bool>{out, false};
                }
            }
            else
            {
                return std::pair<Info, bool>{out, false};
            }
            break;
        }
        case Instruction::LOAD:
        {
            auto src_addr = inst->getUses()[0];
            assert(src_addr->getType()->isPTR());
            assert(src_addr->getEntry()->isVariable() || src_addr->getDef());
            if (src_addr->getEntry()->isVariable() || src_addr->getDef()->isAlloca())
            {
                if (out.scalar2op.count(src_addr->getEntry()) && out.scalar2op[src_addr->getEntry()] != nullptr)
                {
                    inst->replaceAllUsesWith(out.scalar2op[src_addr->getEntry()]);
                    freeInsts.insert(inst);
                }
                // else
                // {
                //     out.scalar2op[src_addr->getEntry()] = inst->getDef();
                // }
            }
            else if (src_addr->getDef()->isGep())
            {
                auto [se, offset] = analyzeGep(src_addr->getDef());
                if (se != nullptr && out.arr2ops.count(se) && out.arr2ops[se].count(offset) && out.arr2ops[se][offset] != nullptr)
                {
                    inst->replaceAllUsesWith(out.arr2ops[se][offset]);
                    freeInsts.insert(inst);
                }
                // else if (se != nullptr && offset != -1)
                // {
                //     out.arr2ops[se][offset] = inst->getDef();
                // }
            }
            break;
        }
        default:
        {
            if (inst->constEval())
            {
                freeInsts.insert(inst);
            }
            break;
        }
        }
    }

    if (is_pre)
    {
        for (auto [se, op] : out.scalar2op)
        {
            if (se->isVariable() && (dynamic_cast<IdentifierSymbolEntry *>(se)->isGlobal() || dynamic_cast<IdentifierSymbolEntry *>(se)->isParam()))
                func_out[bb->getParent()].unknown_scalar.insert(se);
        }
        for (auto se : out.unknown_scalar)
        {
            if (se->isVariable() && (dynamic_cast<IdentifierSymbolEntry *>(se)->isGlobal() || dynamic_cast<IdentifierSymbolEntry *>(se)->isParam()))
                func_out[bb->getParent()].unknown_scalar.insert(se);
        }
        for (auto [se, idx2op] : out.arr2ops)
        {
            if (se->isVariable() && (dynamic_cast<IdentifierSymbolEntry *>(se)->isGlobal() || dynamic_cast<IdentifierSymbolEntry *>(se)->isParam()))
                func_out[bb->getParent()].unknown_arr.insert(se);
        }
        for (auto se : out.unknown_arr)
        {
            if (se->isVariable() && (dynamic_cast<IdentifierSymbolEntry *>(se)->isGlobal() || dynamic_cast<IdentifierSymbolEntry *>(se)->isParam()))
                func_out[bb->getParent()].unknown_arr.insert(se);
        }
    }

    if (!is_global)
    {
        // out与in合并
        for (auto element : in.scalar2op)
        {
            if (!out.scalar2op.count(element.first))
            {
                out.scalar2op.insert(element);
            }
        }
        for (auto element : in.unknown_scalar)
        {
            out.unknown_scalar.insert(element);
        }
        for (auto element : in.arr2ops)
        {
            for (auto [i, op] : element.second)
            {
                if (out.arr2ops.count(element.first) && out.arr2ops[element.first].count(i))
                    ;
                else
                {
                    out.arr2ops[element.first][i] = op;
                }
            }
        }
        for (auto element : in.unknown_arr)
        {
            out.unknown_arr.insert(element);
        }
        for (auto inst : freeInsts)
        {
            delete inst;
        }
    }
    else
    {
        for (auto inst : freeInsts)
        {
            if (inst->isCond() || inst->isCmp())
                delete inst;
        }
    }

    return std::pair<Info, bool>{out, true};
}

void MemoryOpt::pass()
{
    // pre
    for (auto func : unit->getFuncList())
    {
        assert(func->getSymPtr()->isVariable());
        if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() != "main")
        {
            func_out[func];
            for (auto bb : func->getBlockList())
            {
                auto [local_info, success] = transfer(Info(), bb, false, true);
                if (!success)
                {
                    for (auto id_se : unit->getDeclList())
                    {
                        if (id_se->getType()->isARRAY())
                        {
                            auto pointer_se = id_se->getAddr()->getEntry();
                            func_out[func].unknown_arr.insert(pointer_se);
                        }
                        else if (id_se->getType()->isInt() || id_se->getType()->isFloat())
                        {
                            auto pointer_se = id_se->getAddr()->getEntry();
                            func_out[func].unknown_scalar.insert(pointer_se);
                        }
                    }
                    for (auto se : func->getParamsList())
                    {
                        if (se->isVariable() && se->getType()->isPTR())
                            func_out[func].unknown_arr.insert(se);
                    }
                    break;
                }
            }
        }
    }

    for (auto func = unit->begin(); func != unit->end(); func++)
    {
        pass(*func);
    }

    SimplifyCFG sc(unit);
    sc.pass();

    // 删除只存值但不使用的store
    // TODO：考虑store和使用的顺序、考虑数组偏移，删更多store
    std::set<SymbolEntry *> used_arr_se;
    std::set<SymbolEntry *> used_scalar_se;
    bool success = true;
    for (auto func : unit->getFuncList())
    {
        auto all_bbs = func->getBlockList();
        for (auto bb : all_bbs)
        {
            for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
            {
                switch (inst->getInstType())
                {
                case Instruction::LOAD:
                {
                    auto src_addr = inst->getUses()[0];
                    assert(src_addr->getEntry()->isVariable() || src_addr->getDef());
                    if (src_addr->getEntry()->isVariable() || src_addr->getDef()->isAlloca())
                    {
                        used_scalar_se.insert(src_addr->getEntry());
                    }
                    else if (src_addr->getDef()->isGep())
                    {
                        auto [se, offset] = analyzeGep(src_addr->getDef());
                        if (se == nullptr)
                        {
                            success = false;
                        }
                        else
                        {
                            used_arr_se.insert(se);
                        }
                    }
                    else
                    {
                        success = false;
                    }
                    break;
                }
                case Instruction::CALL:
                {
                    for (auto param_op : inst->getUses())
                    {
                        if (param_op->getEntry()->getType()->isPTR())
                        {
                            if (param_op->getDef() == nullptr)
                            {
                                assert(param_op->getEntry()->isVariable());
                                used_arr_se.insert(param_op->getEntry());
                            }
                            else
                            {
                                assert(param_op->getDef()->isAlloca() || param_op->getDef()->isGep() || param_op->getDef()->isPHI() || param_op->getDef()->isLoad());
                                if (param_op->getDef()->isAlloca())
                                    used_arr_se.insert(param_op->getEntry());
                                else if (param_op->getDef()->isGep())
                                {
                                    auto [se, offset] = analyzeGep(param_op->getDef());
                                    if (se == nullptr)
                                    {
                                        success = false;
                                    }
                                    else
                                    {
                                        used_arr_se.insert(se);
                                    }
                                }
                                else
                                {
                                    success = false;
                                }
                            }
                        }
                    }
                    break;
                }

                default:
                    break;
                }
            }
        }
    }
    if (success)
    {
        std::set<Instruction *> freeStore;
        for (auto func : unit->getFuncList())
        {
            for (auto bb : func->getBlockList())
            {
                for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
                {
                    switch (inst->getInstType())
                    {
                    case Instruction::STORE:
                    {
                        auto dst_addr = inst->getUses()[0];
                        assert(dst_addr->getEntry()->isVariable() || dst_addr->getDef());
                        if (dst_addr->getEntry()->isVariable() || dst_addr->getDef()->isAlloca())
                        {
                            if (dst_addr->getEntry()->isVariable() && dynamic_cast<IdentifierSymbolEntry *>(dst_addr->getEntry())->isParam())
                                ;
                            else if (!used_scalar_se.count(dst_addr->getEntry()))
                            {
                                freeStore.insert(inst);
                            }
                        }
                        else if (dst_addr->getDef()->isGep())
                        {
                            auto [se, offset] = analyzeGep(dst_addr->getDef());
                            if (se != nullptr && se->isVariable() && dynamic_cast<IdentifierSymbolEntry *>(se)->isParam())
                                ;
                            else if (se != nullptr && !used_arr_se.count(se))
                            {
                                freeStore.insert(inst);
                            }
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
        }
        for (auto inst : freeStore)
        {
            delete inst;
        }
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

        while (!q.empty())
        {
            auto cur_bb = q.front();
            q.pop_front();

            Info in;

            if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() == "main" && cur_bb == entry)
            {
                for (auto id_se : unit->getDeclList())
                {
                    if (id_se->getType()->isARRAY())
                    {
                        auto pointer_se = id_se->getAddr()->getEntry();
                        if (bb_in[cur_bb].arr2ops.count(pointer_se))
                            in.arr2ops[pointer_se] = bb_in[cur_bb].arr2ops[pointer_se];
                        else
                        {
                            int i = 0;
                            for (auto val : id_se->getArrVals())
                            {
                                in.arr2ops[pointer_se][i] = new Operand(new ConstantSymbolEntry(((ArrayType *)(id_se->getType()))->getElemType(), val));
                                i++;
                                // if (i > maxN)
                                //     break;
                            }
                        }
                    }
                    else if (id_se->getType()->isInt() || id_se->getType()->isFloat())
                    {
                        auto pointer_se = id_se->getAddr()->getEntry();
                        if (bb_in[cur_bb].scalar2op.count(pointer_se) && bb_in[cur_bb].scalar2op[pointer_se] != nullptr && bb_in[cur_bb].scalar2op[pointer_se]->getType() == Var2Const(id_se->getType()) && bb_in[cur_bb].scalar2op[pointer_se]->getEntry()->getValue() == id_se->getValue())
                            in.scalar2op[pointer_se] = bb_in[cur_bb].scalar2op[pointer_se];
                        else
                            in.scalar2op[pointer_se] = new Operand(new ConstantSymbolEntry(id_se->getType(), id_se->getValue()));
                    }
                }
            }
            else if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() != "main" && cur_bb == entry)
            {
                for (auto id_se : unit->getDeclList())
                {
                    if (id_se->getType()->isARRAY())
                    {
                        auto pointer_se = id_se->getAddr()->getEntry();
                        in.unknown_arr.insert(pointer_se);
                    }
                    else if (id_se->getType()->isInt() || id_se->getType()->isFloat())
                    {
                        auto pointer_se = id_se->getAddr()->getEntry();
                        in.unknown_scalar.insert(pointer_se);
                    }
                }
                for (auto se : func->getParamsList())
                {
                    if (se->isVariable() && se->getType()->isPTR())
                        in.unknown_arr.insert(se);
                }
            }
            else
            {
                bool first = true;
                for (auto bb_iter = cur_bb->pred_begin(); bb_iter != cur_bb->pred_end(); bb_iter++)
                {
                    if (bb_out.count(*bb_iter))
                    {
                        if (first)
                        {
                            in = bb_out[*bb_iter];
                            first = false;
                        }
                        else
                            in = meet(in, bb_out[*bb_iter]);
                    }
                }
            }

            // fprintf(stderr, "-------------------\ncur_bb is %d\n-------------------\n", cur_bb->getNo());
            // in.print();

            auto [out, success] = transfer(in, cur_bb, false);

            if (!success)
            {
                auto all_bbs = func->getBlockList();
                for (auto bb : all_bbs)
                {
                    transfer(Info(), bb, false);
                }
                return;
            }

            // out.print();

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
                //     fprintf(stderr, "%d%d\n", bb_out[cur_bb].arr2ops == out.arr2ops, bb_out[cur_bb].scalar2op == out.scalar2op);
                // }
                changed = true;
                bb_in[cur_bb] = in;
                bb_out[cur_bb] = out;
            }
        }

        func->SimplifyPHI();

        // fprintf(stderr, "one iter finished\n");

    } while (changed);

    for (auto bb : func->getBlockList())
        transfer(bb_in[bb], bb, true);

    bb_in.clear();
    bb_out.clear();
}