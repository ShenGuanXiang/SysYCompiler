#include "PureFunc.h"
#include "MemoryOpt.h"

extern std::pair<SymbolEntry *, int> analyzeGep(Instruction *inst);

static std::set<Function *> pureFuncs;
static std::map<Function *, std::set<SymbolEntry *>> func2read;
static std::map<BasicBlock *, std::set<SymbolEntry *>> bb2dirtyse;
static std::set<BasicBlock *> unknownbb2dirty;

static inline bool isImportantSe(SymbolEntry *se)
{
    return (se->isVariable() && (dynamic_cast<IdentifierSymbolEntry *>(se)->isGlobal() || dynamic_cast<IdentifierSymbolEntry *>(se)->isParam()));
}

// get pureFuncs、func2read、bb2dirtyse、unknownbb2dirty
static bool analyzeFunc(Function *func)
{
    bool isPure = true;
    func2read[func];
    for (auto bb : func->getBlockList())
    {
        for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
        {
            for (auto use : inst->getUses())
            {
                if (isImportantSe(use->getEntry()) && use->getEntry()->getType()->isPTR())
                {
                    func2read[func].insert(use->getEntry());
                }
            }
            switch (inst->getInstType())
            {
            case Instruction::STORE:
            {
                auto dst_addr = inst->getUses()[0];
                assert(dst_addr->getType()->isPTR());
                assert(dst_addr->getEntry()->isVariable() || dst_addr->getDef());
                if (dst_addr->getEntry()->isVariable() || dst_addr->getDef()->isAlloca())
                {
                    if (isImportantSe(dst_addr->getEntry()))
                        isPure = false;
                    bb2dirtyse[bb].insert(dst_addr->getEntry());
                }
                else if (dst_addr->getDef()->isGep())
                {
                    auto [se, offset] = analyzeGep(dst_addr->getDef());
                    if (se == nullptr || isImportantSe(se))
                        isPure = false;
                    if (se == nullptr)
                        unknownbb2dirty.insert(bb);
                    else
                        bb2dirtyse[bb].insert(se);
                }
                else
                {
                    isPure = false;
                    unknownbb2dirty.insert(bb);
                }
                break;
            }
            case Instruction::CALL:
            {
                auto callee = dynamic_cast<FuncCallInstruction *>(inst)->getFuncSe();
                if (callee->getName() == "memset")
                {
                    assert(inst->getUses()[0]->getType()->isPTR() && dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType()->isARRAY());
                    auto se = inst->getUses()[0]->getEntry();
                    assert(se->isTemporary()); // 一定是局部数组声明才用到
                    bb2dirtyse[bb].insert(se);
                }
                else if (callee->getName() == "getarray" || callee->getName() == "getfarray")
                {
                    auto param_op = inst->getUses()[0];
                    if (param_op->getDef() == nullptr)
                    {
                        isPure = false; // 如：int f(int a[][2]) { getarray(a);};
                        unknownbb2dirty.insert(bb);
                    }
                    else
                    {
                        assert(param_op->getDef()->isGep() || param_op->getDef()->isPHI());
                        if (param_op->getDef()->isGep())
                        {
                            auto origin_se = analyzeGep(param_op->getDef()).first;
                            if (origin_se == nullptr || isImportantSe(origin_se))
                                isPure = false;
                            if (origin_se == nullptr)
                                unknownbb2dirty.insert(bb);
                            else
                                bb2dirtyse[bb].insert(origin_se);
                        }
                        else
                        {
                            isPure = false;
                            unknownbb2dirty.insert(bb);
                        }
                    }
                }
                else if (callee->getFunction() != nullptr && !callee->isLibFunc() && callee->getFunction() != func && !pureFuncs.count(callee->getFunction()))
                {
                    isPure = false; // 更精细点，还可排除调用函数非纯但只改变参数数组且调用传进去的参数数组为局部
                    unknownbb2dirty.insert(bb);
                }
                else if (callee->isLibFunc())
                {
                    isPure = false;
                }
                break;
            }
            default:
                break;
            }
        }
    }
    if (isPure)
        pureFuncs.insert(func);
    return isPure;
}

static std::map<BasicBlock *, std::set<BasicBlock *>> domtree;
static std::map<std::string, std::vector<Operand *>> htable;

static std::string getHash(FuncCallInstruction *funcCall)
{
    auto dst_type = funcCall->getDef()->getType()->toStr();
    auto func_name = funcCall->getFuncSe()->toStr();
    std::string hashCode = dst_type + " " + func_name;
    for (size_t i = 0; i != funcCall->getUses().size(); i++)
    {
        std::string src = funcCall->getUses()[i]->toStr();
        std::string src_type = funcCall->getUses()[i]->getType()->toStr();
        hashCode += " " + src_type + " " + src;
    }
    return hashCode;
}

static std::map<std::pair<BasicBlock *, BasicBlock *>, std::vector<std::vector<BasicBlock *>>> all_paths;

static void dfs_path(BasicBlock *cur_bb, BasicBlock *to, std::map<BasicBlock *, int> &visited, std::vector<BasicBlock *> &cur_path)
{
    if (!visited.count(cur_bb))
    {
        visited[cur_bb] = 1;
    }
    else
    {
        visited[cur_bb]++;
    }
    cur_path.push_back(cur_bb);

    if (cur_bb == to)
    {
        all_paths[std::make_pair(cur_path[0], to)].push_back(cur_path);
    }

    for (auto bb_iter = cur_bb->succ_begin(); bb_iter != cur_bb->succ_end(); bb_iter++)
    {
        auto succ_bb = *bb_iter;
        if (!visited.count(succ_bb) || ((succ_bb == to || succ_bb == cur_path[0]) && visited[succ_bb] == 1))
            dfs_path(succ_bb, to, visited, cur_path);
    }

    cur_path.pop_back();
}

static std::vector<std::vector<BasicBlock *>> getPaths(BasicBlock *from, BasicBlock *to)
{
    if (all_paths.count(std::make_pair(from, to)))
    {
        return all_paths[std::make_pair(from, to)];
    }

    auto visited = std::map<BasicBlock *, int>();
    auto cur_path = std::vector<BasicBlock *>();
    dfs_path(from, to, visited, cur_path);
    assert(all_paths.count(std::make_pair(from, to)));
    return all_paths[std::make_pair(from, to)];
}

// <se, is_dirty>
static std::pair<SymbolEntry *, bool> getDirtySe(Instruction *inst)
{
    switch (inst->getInstType())
    {
    case Instruction::STORE:
    {
        auto dst_addr = inst->getUses()[0];
        assert(dst_addr->getType()->isPTR());
        assert(dst_addr->getEntry()->isVariable() || dst_addr->getDef());
        if (dst_addr->getEntry()->isVariable() || dst_addr->getDef()->isAlloca())
        {
            return {dst_addr->getEntry(), true};
        }
        else if (dst_addr->getDef()->isGep())
        {
            auto [se, offset] = analyzeGep(dst_addr->getDef());
            return {se, true};
        }
        else
        {
            return {nullptr, true};
        }
        break;
    }
    case Instruction::CALL:
    {
        auto callee = dynamic_cast<FuncCallInstruction *>(inst)->getFuncSe();
        if (callee->getName() == "memset")
        {
            assert(inst->getUses()[0]->getType()->isPTR() && dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType()->isARRAY());
            auto se = inst->getUses()[0]->getEntry();
            assert(se->isTemporary()); // 一定是局部数组声明才用到
            return {se, true};
        }
        else if (callee->getName() == "getarray" || callee->getName() == "getfarray")
        {
            auto param_op = inst->getUses()[0];
            if (param_op->getDef() == nullptr)
            {
                return {nullptr, true};
            }
            else
            {
                assert(param_op->getDef()->isGep() || param_op->getDef()->isPHI());
                if (param_op->getDef()->isGep())
                {
                    auto origin_se = analyzeGep(param_op->getDef()).first;
                    return {origin_se, true};
                }
                else
                {
                    return {nullptr, true};
                }
            }
        }
        else if (callee->getFunction() != nullptr && !callee->isLibFunc() && !pureFuncs.count(callee->getFunction()))
        {
            return {nullptr, true};
        }
        break;
    }
    default:
        break;
    }
    return {nullptr, false};
}

static void dfs_clear(BasicBlock *bb)
{
    auto prehtable = htable;

    std::set<Instruction *> freeCall;
    for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
    {
        if (inst->isCall())
        {
            // inst->output();
            auto callInst = dynamic_cast<FuncCallInstruction *>(inst);
            if (pureFuncs.count(callInst->getFuncSe()->getFunction()))
            {
                if (htable.count(getHash(callInst)))
                {
                    // 获取后者需要读的内存
                    std::set<SymbolEntry *> need_to_read;
                    bool success = true;
                    for (auto se : func2read[callInst->getFuncSe()->getFunction()])
                    {
                        assert(se->isVariable());
                        auto id_se = dynamic_cast<IdentifierSymbolEntry *>(se);
                        if (id_se->isParam())
                        {
                            auto param_op = callInst->getUses()[id_se->getParamNo()];
                            if (param_op->getDef() == nullptr)
                            {
                                assert(param_op->getEntry()->isVariable());
                                need_to_read.insert(param_op->getEntry());
                            }
                            else
                            {
                                assert(param_op->getDef()->isAlloca() || param_op->getDef()->isGep() || param_op->getDef()->isPHI() || param_op->getDef()->isLoad());
                                if (param_op->getDef()->isAlloca())
                                    need_to_read.insert(param_op->getEntry());
                                else if (param_op->getDef()->isGep())
                                {
                                    auto origin_se = analyzeGep(param_op->getDef()).first;
                                    if (origin_se == nullptr)
                                    {
                                        success = false;
                                        break;
                                    }
                                    else
                                        need_to_read.insert(origin_se);
                                }
                                else
                                {
                                    success = false;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            assert(id_se->isGlobal());
                            need_to_read.insert(se);
                        }
                    }
                    if (!success)
                        continue;

                    // 对每个已保存的纯函数调用，遍历两者间的所有路径，判断是否有改变后者需要读的内存
                    for (auto src_op : htable[getHash(callInst)])
                    {
                        auto from = src_op->getDef()->getParent();
                        auto paths = getPaths(from, bb);
                        std::set<BasicBlock *> path_bbs;
                        for (auto path : paths)
                        {
                            for (int i = 1; i < path.size() - 1; i++)
                            {
                                path_bbs.insert(path[i]);
                            }
                        }
                        for (auto path_bb : path_bbs)
                        {
                            if (unknownbb2dirty.count(path_bb))
                            {
                                success = false;
                                break;
                            }
                            else
                            {
                                for (auto dirty_se : bb2dirtyse[path_bb])
                                {
                                    if (need_to_read.count(dirty_se))
                                    {
                                        success = false;
                                        break;
                                    }
                                }
                            }
                        }
                        if (!path_bbs.count(from))
                        {
                            for (auto dirty_inst = src_op->getDef(); dirty_inst != from->end(); dirty_inst = dirty_inst->getNext())
                            {
                                auto [dirty_se, is_dirty] = getDirtySe(dirty_inst);
                                if (is_dirty && (dirty_se == nullptr || need_to_read.count(dirty_se)))
                                {
                                    success = false;
                                    break;
                                }
                            }
                        }
                        if (!path_bbs.count(bb))
                        {
                            for (auto dirty_inst = bb->begin(); dirty_inst != inst; dirty_inst = dirty_inst->getNext())
                            {
                                auto [dirty_se, is_dirty] = getDirtySe(dirty_inst);
                                if (is_dirty && (dirty_se == nullptr || need_to_read.count(dirty_se)))
                                {
                                    success = false;
                                    break;
                                }
                            }
                        }

                        if (success)
                        {
                            inst->replaceAllUsesWith(src_op);
                            freeCall.insert(inst);
                            break;
                        }
                    }
                }
                else
                {
                    htable[getHash(callInst)].push_back(inst->getDef());
                }
            }
        }
    }

    for (auto inst : freeCall)
    {
        delete inst;
    }

    for (auto it_child = domtree[bb].rbegin(); it_child != domtree[bb].rend(); it_child++)
        dfs_clear(*it_child);
    htable = prehtable;
}

static void clearRedundantPureCall(Function *func)
{
    // compute domtree
    domtree.clear();
    func->ComputeDom();
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        BasicBlock *bb = *it_bb;
        if (bb == func->getEntry())
            continue;
        domtree[bb->getIDom()].insert(bb);
    }

    all_paths.clear();
    htable.clear();
    dfs_clear(func->getEntry());

    // 清理返回值没被用过的纯函数调用
    std::set<Instruction *> freeCall;
    for (auto bb : func->getBlockList())
    {
        for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
        {
            if (inst->isCall())
            {
                auto callInst = dynamic_cast<FuncCallInstruction *>(inst);
                if (pureFuncs.count(callInst->getFuncSe()->getFunction()) && (inst->hasNoDef() || inst->getDef()->getUses().empty()))
                {
                    freeCall.insert(inst);
                }
            }
        }
    }
    for (auto inst : freeCall)
    {
        delete inst;
    }
}

void replaceLoadByNearestLoad(BasicBlock *bb)
{
    std::map<Operand *, Operand *> op2op;
    std::set<Instruction *> freeLoad;
    for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
    {
        if (inst->isLoad())
        {
            if (op2op.count(inst->getUses()[0]))
            {
                freeLoad.insert(inst);
                inst->replaceAllUsesWith(op2op[inst->getUses()[0]]);
            }
            else
                op2op[inst->getUses()[0]] = inst->getDef();
        }
        else
        {
            auto [dirty_se, is_dirty] = getDirtySe(inst);
            if (is_dirty)
            {
                op2op.clear(); // TODO：具体到去除某一个Operand*
            }
        }
    }
    for (auto inst : freeLoad)
        delete inst;
}

void PureFunc::pass()
{
    funcElim();
    pureFuncs.clear();
    func2read.clear();
    bb2dirtyse.clear();
    unknownbb2dirty.clear();
    for (auto func : unit->getFuncList())
    {
        analyzeFunc(func);
    }

    for (auto func : unit->getFuncList())
    {
        for (auto bb : func->getBlockList())
        {
            replaceLoadByNearestLoad(bb);
        }
    }

    for (auto func : unit->getFuncList())
    {
        clearRedundantPureCall(func);
    }
}

void PureFunc::funcElim()
{
    unit->getCallGraph();
    for (auto func_it = unit->begin(); func_it != unit->end(); func_it++)
    {
        Function *func = *func_it;
        if (dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr())->getName() == "main")
            continue;
        bool is_useless = true;
        for (auto call_i : func->getCallersInsts())
        {
            FuncCallInstruction *call = dynamic_cast<FuncCallInstruction *>(call_i);
            if (call->hasNoDef())
                continue;
            if (call->getDef()->getUses().empty())
                continue;
            else
            {
                is_useless = false;
                break;
            }
        }
        if (is_useless)
        {
            if (dynamic_cast<FunctionType *>(func->getSymPtr()->getType())->getRetType()->isVoid())
                continue;
            for (auto bb_it = func->begin(); bb_it != func->end(); bb_it++)
            {
                BasicBlock *bb = *bb_it;
                for (auto inst = bb->rbegin(); inst != bb->rend(); inst = inst->getPrev())
                {
                    if (inst->isRet())
                    {
                        assert(inst->getUses().size() == 1);
                        // inst->output();
                        Operand *retop = inst->getUses()[0];
                        bb->insertBefore(new RetInstruction(new Operand(new ConstantSymbolEntry(retop->getType(), 0))), inst);
                        bb->remove(inst);
                        delete inst;
                        break;
                    }
                }
            }
        }
    }
}
