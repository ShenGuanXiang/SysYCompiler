#include "Mem2Reg.h"
#include "SimplifyCFG.h"
#include "Type.h"
#include <queue>

bool mem2reg = false;

static std::map<AllocaInstruction *, bool> allocaPromotable;
static std::map<Instruction *, unsigned> InstNumbers;
static std::set<Instruction *> freeInsts;

struct AddrInfo
{

    std::vector<BasicBlock *> DefiningBlocks;
    std::vector<BasicBlock *> UsingBlocks;

    StoreInstruction *OnlyStore;
    BasicBlock *OnlyBlock;
    bool OnlyUsedInOneBlock;

    void clear()
    {
        DefiningBlocks.clear();
        UsingBlocks.clear();
        OnlyStore = nullptr;
        OnlyBlock = nullptr;
        OnlyUsedInOneBlock = true;
    }

    // 计算哪里的基本块定义(store)和使用(load)了addr变量
    void AnalyzeAddr(Operand *addr)
    {
        clear();
        // 获得store指令和load指令所在的基本块，并判断它们是否在同一块中
        for (auto user : addr->getUses())
        {
            if (user->isStore())
            {
                DefiningBlocks.push_back(user->getParent());
                OnlyStore = dynamic_cast<StoreInstruction *>(user);
            }
            else
            {
                assert(user->isLoad());
                UsingBlocks.push_back(user->getParent());
            }

            if (OnlyUsedInOneBlock)
            {
                if (!OnlyBlock)
                    OnlyBlock = user->getParent();
                else if (OnlyBlock != user->getParent())
                    OnlyUsedInOneBlock = false;
            }
        }
    }
};

void Mem2Reg::pass()
{
    global2Local();
    for (auto func = unit->begin(); func != unit->end(); func++)
    {
        (*func)->ComputeDom();
        (*func)->ComputeDomFrontier();
        InsertPhi(*func);
        Rename(*func);
    }
    mem2reg = true;
}

void Mem2Reg::global2Local()
{
    unit->getCallGraph();
    auto main_func = unit->getMainFunc();
    for (auto id_se : unit->getDeclList())
    {
        if (id_se->getType()->isARRAY() || id_se->getType()->isFunc()) // TODO ：数组全局转局部
            continue;
        if (id_se->getAddr() == nullptr)
        {
            unit->removeDecl(id_se);
            continue;
        }
        std::set<SymbolEntry *> userFuncs;
        for (auto userInst : id_se->getAddr()->getUses())
        {
            assert(userInst->isLoad() || userInst->isStore());
            userFuncs.insert(userInst->getParent()->getParent()->getSymPtr());
            for (auto func : userInst->getParent()->getParent()->getCallers())
            {
                userFuncs.insert(func->getSymPtr());
            }
        }
        // 优化main函数开头的全局int/float变量的读写
        std::vector<Instruction *> useless_load;
        for (auto inst = main_func->getEntry()->begin(); inst != main_func->getEntry()->end(); inst = inst->getNext())
        {
            if (inst->isStore() && inst->getUses()[0] == id_se->getAddr() && inst->getUses()[1]->getType()->isConst())
            {
                id_se->setValue(inst->getUses()[1]->getEntry()->getValue());
                delete inst;
                break;
            }
            else if (inst->isLoad() && inst->getUses()[0] == id_se->getAddr())
            {
                inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(Var2Const(id_se->getType()), id_se->getValue())));
                useless_load.push_back(inst);
            }
            else if (inst->isStore() && inst->getUses()[0] == id_se->getAddr())
            {
                break;
            }
            else if (inst->isCall() && userFuncs.count(dynamic_cast<FuncCallInstruction *>(inst)->getFuncSe()))
            {
                break;
            }
        }
        for (auto inst : useless_load)
            delete inst;
        bool has_store = false, only_in_main = true;
        for (auto userInst : id_se->getAddr()->getUses())
        {
            assert(userInst->isLoad() || userInst->isStore());
            if (userInst->isStore())
                has_store = true;
            if (userInst->getParent()->getParent() != main_func) // TODO ：这里应该还有优化空间
                only_in_main = false;
        }
        // 对于全局从未发生store的全局变量，将其视为常数处理。
        if (!has_store)
        {
            for (auto userInst : id_se->getAddr()->getUses())
            {
                assert(userInst->isLoad());
                assert(id_se->getType()->isFloat() || id_se->getType()->isInt());
                double value = id_se->getType()->isFloat() ? (float)id_se->getValue() : (int)id_se->getValue();
                auto replVal = new Operand(new ConstantSymbolEntry(Var2Const(id_se->getType()), value));
                userInst->replaceAllUsesWith(replVal);
                userInst->getParent()->remove(userInst);
                freeInsts.insert(userInst);
            }
            unit->removeDecl(id_se);
        }
        // 对于全局int/float类型变量，转换为函数内的局部变量。
        else if (only_in_main)
        {
            auto old_addr = id_se->getAddr();
            auto new_addr = new Operand(new TemporarySymbolEntry(new PointerType(id_se->getType()), SymbolTable::getLabel()));
            id_se->setScope(IdentifierSymbolEntry::LOCAL);
            id_se->setAddr(new_addr);
            main_func->getEntry()->insertFront(new AllocaInstruction(new_addr, id_se));
            Instruction *last_alloc_pos = main_func->getEntry()->begin();
            for (; last_alloc_pos->isAlloca() && last_alloc_pos != main_func->getEntry()->end(); last_alloc_pos = last_alloc_pos->getNext())
                ;
            double value = id_se->getType()->isFloat() ? (float)id_se->getValue() : (int)id_se->getValue();
            main_func->getEntry()->insertBefore(new StoreInstruction(new_addr, new Operand(new ConstantSymbolEntry(Var2Const(id_se->getType()), value))), last_alloc_pos);
            std::vector<Instruction *> userInsts;
            for (auto userInst : old_addr->getUses())
            {
                userInsts.push_back(userInst);
            }
            for (auto userInst : userInsts)
            {
                userInst->replaceUsesWith(old_addr, new_addr);
            }
            unit->removeDecl(id_se);
        }
    }
    for (auto inst : freeInsts)
        delete inst;
    freeInsts.clear();
}

// SDoms & IDom
void Function::ComputeDom()
{
    // 删除不可达的基本块。
    std::set<BasicBlock *> visited;
    std::queue<BasicBlock *> q1;
    q1.push(getEntry());
    visited.insert(getEntry());
    while (!q1.empty())
    {
        auto bb = q1.front();
        std::vector<BasicBlock *> succs(bb->succ_begin(), bb->succ_end());
        q1.pop();
        for (auto succ : succs)
        {
            if (!visited.count(succ))
            {
                q1.push(succ);
                visited.insert(succ);
            }
        }
    }
    auto block_list_copy = getBlockList();
    for (auto bb : block_list_copy)
    {
        if (!visited.count(bb))
            delete bb;
    }

    // Vertex-removal Algorithm, O(n^2)
    for (auto bb : getBlockList())
        bb->getSDoms() = std::set<BasicBlock *>();
    std::set<BasicBlock *> all_bbs(getBlockList().begin(), getBlockList().end());
    for (auto removed_bb : getBlockList())
    {
        std::set<BasicBlock *> visited;
        std::queue<BasicBlock *> q;
        std::map<BasicBlock *, bool> is_visited;
        for (auto bb : getBlockList())
            is_visited[bb] = false;
        if (getEntry() != removed_bb)
        {
            visited.insert(getEntry());
            is_visited[getEntry()] = true;
            q.push(getEntry());
            while (!q.empty())
            {
                BasicBlock *cur = q.front();
                q.pop();
                for (auto succ = cur->succ_begin(); succ != cur->succ_end(); succ++)
                    if (*succ != removed_bb && !is_visited[*succ])
                    {
                        q.push(*succ);
                        visited.insert(*succ);
                        is_visited[*succ] = true;
                    }
            }
        }
        std::set<BasicBlock *> not_visited;
        set_difference(all_bbs.begin(), all_bbs.end(), visited.begin(), visited.end(), inserter(not_visited, not_visited.end()));
        for (auto bb : not_visited)
        {
            if (bb != removed_bb)
                bb->getSDoms().insert(removed_bb); // strictly dominators
        }
    }
    // immediate dominator ：严格支配 bb，且不严格支配任何严格支配 bb 的节点的节点
    std::set<BasicBlock *> temp_IDoms;
    for (auto bb : getBlockList())
    {
        temp_IDoms = bb->getSDoms();
        for (auto sdom : bb->getSDoms())
        {
            std::set<BasicBlock *> diff_set;
            set_difference(temp_IDoms.begin(), temp_IDoms.end(), sdom->getSDoms().begin(), sdom->getSDoms().end(), inserter(diff_set, diff_set.end()));
            temp_IDoms = diff_set;
        }
        assert(temp_IDoms.size() == 1 || (bb == getEntry() && temp_IDoms.size() == 0));
        if (bb != getEntry())
            bb->getIDom() = *temp_IDoms.begin();
    }
    // for (auto bb : getBlockList())
    //     if (bb != getEntry())
    //         fprintf(stderr, "IDom[B%d] = B%d\n", bb->getNo(), bb->getIDom()->getNo());
}

// ref : Static Single Assignment Book
void Function::ComputeDomFrontier()
{
    for (auto bb : getBlockList())
        bb->getDomFrontiers() = std::set<BasicBlock *>();
    std::map<BasicBlock *, bool> is_visited;
    for (auto bb : getBlockList())
        is_visited[bb] = false;
    std::queue<BasicBlock *> q;
    q.push(getEntry());
    is_visited[getEntry()] = true;
    while (!q.empty())
    {
        auto a = q.front();
        q.pop();
        std::vector<BasicBlock *> succs(a->succ_begin(), a->succ_end());
        for (auto b : succs)
        {
            auto x = a;
            while (!b->getSDoms().count(x))
            {
                assert(x != getEntry());
                x->getDomFrontiers().insert(b);
                x = x->getIDom();
            }
            if (!is_visited[b])
            {
                is_visited[b] = true;
                q.push(b);
            }
        }
    }
    // for (auto BB : getBlockList())
    // {
    //     fprintf(stderr, "DF[B%d] = {", BB->getNo());
    //     for (auto bb : BB->getDomFrontiers())
    //         fprintf(stderr, "B%d, ", bb->getNo());
    //     fprintf(stderr, "}\n");
    // }
}

void Instruction::replaceAllUsesWith(Operand *replVal)
{
    if (def_list.empty())
        return;
    for (auto userInst : def_list[0]->getUses())
    {
        // userInst->output();
        userInst->replaceUsesWith(def_list[0], replVal);
        // userInst->output();
    }
}

static bool isAllocaPromotable(AllocaInstruction *alloca)
{
    if (allocaPromotable.count(alloca))
        return allocaPromotable[alloca];
    if (dynamic_cast<PointerType *>(alloca->getDef()->getEntry()->getType())->isARRAY())
    {
        allocaPromotable[alloca] = false;
        return false; // TODO ：数组拟采用sroa、向量化优化
    }
    auto users = alloca->getDef()->getUses();
    for (auto &user : users)
    {
        // store:不允许alloc的结果作为store的左操作数；不允许store dst的类型和alloc dst的类型不符
        if (user->isStore())
        {
            assert(user->getUses()[1]->getEntry() != alloca->getDef()->getEntry());
            assert(dynamic_cast<PointerType *>(user->getUses()[0]->getType())->getValType() == dynamic_cast<PointerType *>(alloca->getDef()->getType())->getValType());
        }
        // load: 不允许load src的类型和alloc dst的类型不符
        else if (user->isLoad())
        {
            assert(dynamic_cast<PointerType *>(user->getUses()[0]->getType())->getValType() == dynamic_cast<PointerType *>(alloca->getDef()->getType())->getValType());
        }
        // else if (user->isGep())
        else
        {
            allocaPromotable[alloca] = false;
            return false;
        }
    }
    allocaPromotable[alloca] = true;
    return true;
}

static bool isInterestingInstruction(Instruction *inst)
{
    return ((inst->isLoad() && inst->getUses()[0]->getDef() && inst->getUses()[0]->getDef()->isAlloca()) ||
            (inst->isStore() && inst->getUses()[0]->getDef() && inst->getUses()[0]->getDef()->isAlloca()));
}

// 用 lazy 的方式计算指令相对顺序标号
static unsigned getInstructionIndex(Instruction *inst)
{
    assert(isInterestingInstruction(inst) &&
           "Not a load/store from/to an alloca?");

    auto it = InstNumbers.find(inst);
    if (it != InstNumbers.end())
        return it->second;

    BasicBlock *bb = inst->getParent();
    unsigned InstNo = 0;
    for (auto i = bb->begin(); i != bb->end(); i = i->getNext())
        if (isInterestingInstruction(i))
            InstNumbers[i] = InstNo++;
    it = InstNumbers.find(inst);

    assert(it != InstNumbers.end() && "Didn't insert instruction?");
    return it->second;
}

// 如果只有一个store语句，那么被这个store指令所支配的所有指令都要被替换为store的src。
static bool rewriteSingleStoreAlloca(AllocaInstruction *alloca, AddrInfo &Info)
{
    StoreInstruction *OnlyStore = Info.OnlyStore;
    bool StoringGlobalVal = OnlyStore->getUses()[1]->getEntry()->isVariable() &&
                            dynamic_cast<IdentifierSymbolEntry *>(OnlyStore->getUses()[1]->getEntry())->isGlobal();
    BasicBlock *StoreBB = OnlyStore->getParent();
    int StoreIndex = -1;

    Info.UsingBlocks.clear();

    auto AllocaUsers = alloca->getDef()->getUses();
    for (auto &UserInst : AllocaUsers)
    {
        if (UserInst == OnlyStore)
            continue;
        assert(UserInst->isLoad());
        // store 全局变量一定可以处理
        if (!StoringGlobalVal)
        {
            // store 和 load 在同一个块中，则 store 应该在 load 前面
            if (UserInst->getParent() == StoreBB)
            {
                if (StoreIndex == -1)
                    StoreIndex = getInstructionIndex(OnlyStore);

                if (unsigned(StoreIndex) > getInstructionIndex(UserInst))
                {
                    Info.UsingBlocks.push_back(StoreBB);
                    continue;
                }
            }
            // 如果二者在不同基本块，则需要保证 load 指令能被 store 支配
            else if (!(UserInst->getParent()->getSDoms()).count(StoreBB))
            {
                Info.UsingBlocks.push_back(UserInst->getParent());
                continue;
            }
        }

        auto ReplVal = OnlyStore->getUses()[1];
        UserInst->replaceAllUsesWith(ReplVal); // 替换掉load的所有user
        InstNumbers.erase(UserInst);
        UserInst->getParent()->remove(UserInst);
        freeInsts.insert(UserInst); // 删除load指令
    }

    // 有 alloca 支配不到的 User
    if (Info.UsingBlocks.size())
        return false;

    // 移除处理好的store和alloca
    InstNumbers.erase(Info.OnlyStore);
    Info.OnlyStore->getParent()->remove(Info.OnlyStore);
    freeInsts.insert(Info.OnlyStore);
    alloca->getParent()->remove(alloca);
    freeInsts.insert(alloca);
    return true;
}

// 如果某局部变量的读/写(load/store)都只存在一个基本块中，load被之前离他最近的store的右值替换
static bool promoteSingleBlockAlloca(AllocaInstruction *alloca)
{
    // 找到所有store指令的顺序
    using StoresByIndexTy = std::vector<std::pair<unsigned, StoreInstruction *>>;
    StoresByIndexTy StoresByIndex;
    using LoadsByIndexTy = std::vector<std::pair<unsigned, LoadInstruction *>>;
    LoadsByIndexTy LoadsByIndex;

    auto AllocaUsers = alloca->getDef()->getUses();
    for (auto &UserInst : AllocaUsers)
    {
        if (UserInst->isStore())
            StoresByIndex.push_back(std::make_pair(getInstructionIndex(UserInst), dynamic_cast<StoreInstruction *>(UserInst)));
        else
        {
            assert(UserInst->isLoad());
            LoadsByIndex.push_back(std::make_pair(getInstructionIndex(UserInst), dynamic_cast<LoadInstruction *>(UserInst)));
        }
    }
    std::sort(StoresByIndex.begin(), StoresByIndex.end());
    std::sort(LoadsByIndex.begin(), LoadsByIndex.end());

    // 遍历所有load指令，用前面最近的store指令替换掉
    for (auto kv : LoadsByIndex)
    {
        unsigned LoadIdx = kv.first;
        auto LoadInst = kv.second;

        // 找到离load最近的store，用store的操作数替换load的user
        StoresByIndexTy::iterator it = std::lower_bound(StoresByIndex.begin(), StoresByIndex.end(), std::make_pair(LoadIdx, static_cast<StoreInstruction *>(nullptr)));
        if (it == StoresByIndex.begin())
        {
            assert(0 && "Load before Store?");
            // if (StoresByIndex.size())
            //     LoadInst->replaceAllUsesWith((*it).second->getUses()[1]);
            // else
            //     return false;
        }
        else
        {
            auto ReplVal = (*(it - 1)).second->getUses()[1];
            LoadInst->replaceAllUsesWith(ReplVal);
        }

        // 删除load指令
        InstNumbers.erase(LoadInst);
        LoadInst->getParent()->remove(LoadInst);
        freeInsts.insert(LoadInst);
    }

    // 删除alloca和所有的store指令
    alloca->getParent()->remove(alloca);
    freeInsts.insert(alloca);
    for (auto kv : StoresByIndex)
    {
        InstNumbers.erase(kv.second);
        kv.second->getParent()->remove(kv.second);
        freeInsts.insert(kv.second);
    }
    return true;
}

static bool StoreBeforeLoad(BasicBlock *BB, AllocaInstruction *alloca)
{
    for (auto I = BB->begin(); I != BB->end(); I = I->getNext())
        if (I->isLoad() && I->getUses()[0] == alloca->getDef())
            return false;
        else if (I->isStore() && I->getUses()[0] == alloca->getDef())
            return true;
    return false;
}

// 对于一个alloca，检查每一个usingblock，是否在对这个变量load之前有store，有则说明原来的alloca变量被覆盖了，不是live in的
static std::set<BasicBlock *> ComputeLiveInBlocks(AllocaInstruction *alloca, AddrInfo &Info)
{
    std::set<BasicBlock *> UseBlocks(Info.UsingBlocks.begin(), Info.UsingBlocks.end());
    std::set<BasicBlock *> DefBlocks(Info.DefiningBlocks.begin(), Info.DefiningBlocks.end());
    std::set<BasicBlock *> BlocksToCheck;
    set_intersection(UseBlocks.begin(), UseBlocks.end(), DefBlocks.begin(), DefBlocks.end(), inserter(BlocksToCheck, BlocksToCheck.end()));
    std::set<BasicBlock *> LiveInBlocks = UseBlocks;
    for (auto BB : BlocksToCheck)
    {
        if (StoreBeforeLoad(BB, alloca))
            LiveInBlocks.erase(BB);
    }
    // bfs，迭代添加前驱
    std::queue<BasicBlock *> worklist;
    std::map<BasicBlock *, bool> is_visited;
    for (auto bb : alloca->getParent()->getParent()->getBlockList())
        is_visited[bb] = false;
    for (auto bb : LiveInBlocks)
    {
        worklist.push(bb);
        is_visited[bb] = true;
    }
    while (!worklist.empty())
    {
        auto bb = worklist.front();
        worklist.pop();
        for (auto pred = bb->pred_begin(); pred != bb->pred_end(); pred++)
            if (!is_visited[*pred] && (!DefBlocks.count(*pred) || !StoreBeforeLoad(*pred, alloca)))
            {
                LiveInBlocks.insert(*pred);
                worklist.push(*pred);
                is_visited[*pred] = true;
            }
    }
    return LiveInBlocks;
}

// 将局部变量由内存提升到寄存器的主体函数
void Mem2Reg::InsertPhi(Function *func)
{
    AddrInfo Info;
    for (auto inst = func->getEntry()->begin(); inst != func->getEntry()->end(); inst = inst->getNext())
    {
        if (!inst->isAlloca())
            continue;
        auto alloca = dynamic_cast<AllocaInstruction *>(inst);
        if (!isAllocaPromotable(alloca))
            continue;
        assert(alloca->getDef()->getType()->isPTR());

        // 计算哪里的基本块定义(store)和使用(load)了alloc变量
        Info.AnalyzeAddr(alloca->getDef());

        // 筛1：如果alloca出的空间从未被使用，直接删除
        if (Info.UsingBlocks.empty())
        {
            alloca->getParent()->remove(alloca);
            freeInsts.insert(alloca);
            for (auto userInst : alloca->getDef()->getUses())
            {
                assert(userInst->isStore());
                userInst->getParent()->remove(userInst);
                freeInsts.insert(userInst);
            }
            continue;
        }

        // 筛2：如果 alloca 只有一个 store，那么 users 可以替换成这个 store 的值
        if (Info.DefiningBlocks.size() == 1)
        {
            if (rewriteSingleStoreAlloca(alloca, Info))
                continue;
        }

        // 筛3：如果某局部变量的读/写(load/store)都只存在一个基本块中，load要被之前离他最近的store的右值替换
        if (Info.OnlyUsedInOneBlock && promoteSingleBlockAlloca(alloca))
            continue;

        // pruned SSA
        auto LiveInBlocks = ComputeLiveInBlocks(alloca, Info);

        // bfs, insert PHI
        std::map<BasicBlock *, bool> is_visited;
        for (auto bb : func->getBlockList())
            is_visited[bb] = false;
        std::queue<BasicBlock *> worklist;
        for (auto bb : Info.DefiningBlocks)
            worklist.push(bb);
        while (!worklist.empty())
        {
            auto bb = worklist.front();
            worklist.pop();
            for (auto df : bb->getDomFrontiers())
                if (!is_visited[df])
                {
                    if (LiveInBlocks.find(df) != LiveInBlocks.end())
                    {
                        auto phi = new PhiInstruction(alloca->getDef(), true); // 现在PHI的dst是PTR
                        df->insertFront(phi);
                    }
                    is_visited[df] = true;
                    worklist.push(df);
                }
        }
    }
}

// 删除源操作数均相同的PHI
void Function::SimplifyPHI()
{
    for (auto bb : getBlockList())
    {
        for (auto phi = bb->begin(); phi != bb->end() && phi->isPHI(); phi = phi->getNext())
        {
            auto srcs = phi->getUses();
            auto last_src = srcs[0];
            bool Elim = true;
            for (auto src : srcs)
            {
                if (src != last_src && !(src->getType()->isConst() && last_src->getType()->isConst() && src->getEntry()->getValue() == last_src->getEntry()->getValue()))
                {
                    Elim = false;
                    break;
                }
            }
            if (Elim)
            {
                phi->replaceAllUsesWith(last_src);
                delete phi;
            }
        }
    }
}

// https://roife.github.io/2022/02/07/mem2reg/
void Mem2Reg::Rename(Function *func)
{
    using RenamePassData = std::pair<BasicBlock *, std::map<Operand *, Operand *>>; //(bb, addr2val)

    // bfs
    std::map<BasicBlock *, bool> isVisited;
    for (auto bb : func->getBlockList())
        isVisited[bb] = false;
    std::queue<RenamePassData> worklist;
    worklist.push(std::make_pair(func->getEntry(), std::map<Operand *, Operand *>()));
    while (!worklist.empty())
    {
        auto BB = worklist.front().first;
        auto IncomingVals = worklist.front().second;
        worklist.pop();
        if (isVisited[BB])
            continue;
        isVisited[BB] = true;
        for (auto inst = BB->begin(); inst != BB->end(); inst = inst->getNext())
        {
            if (inst->isAlloca())
            {
                if (isAllocaPromotable(dynamic_cast<AllocaInstruction *>(inst)))
                {
                    inst->getParent()->remove(inst);
                    freeInsts.insert(inst);
                }
            }
            else if (inst->isStore())
            {
                if (inst->getUses()[0]->getDef() && inst->getUses()[0]->getDef()->isAlloca() &&
                    isAllocaPromotable(dynamic_cast<AllocaInstruction *>(inst->getUses()[0]->getDef())))
                {
                    IncomingVals[inst->getUses()[0]] = inst->getUses()[1];
                    inst->getParent()->remove(inst);
                    freeInsts.insert(inst);
                }
            }
            else if (inst->isLoad())
            {
                if (inst->getUses()[0]->getDef() && inst->getUses()[0]->getDef()->isAlloca() &&
                    isAllocaPromotable(dynamic_cast<AllocaInstruction *>(inst->getUses()[0]->getDef())))
                {
                    inst->replaceAllUsesWith(IncomingVals[inst->getUses()[0]]);
                    inst->getParent()->remove(inst);
                    freeInsts.insert(inst);
                }
            }
            else if (inst->isPHI())
            {
                if (dynamic_cast<PhiInstruction *>(inst)->get_incomplete())
                {
                    auto new_dst = new Operand(new TemporarySymbolEntry(dynamic_cast<PointerType *>(inst->getDef()->getType())->getValType(),
                                                                        /*dynamic_cast<TemporarySymbolEntry *>(inst->getDef()->getEntry())->getLabel()*/
                                                                        SymbolTable::getLabel()));
                    IncomingVals[inst->getDef()] = new_dst;
                    dynamic_cast<PhiInstruction *>(inst)->updateDst(new_dst); // i32*->i32
                }
            }
        }
        for (auto succ = BB->succ_begin(); succ != BB->succ_end(); succ++)
        {
            worklist.push(std::make_pair(*succ, IncomingVals));
            for (auto phi = (*succ)->begin(); phi != (*succ)->end() && phi->isPHI(); phi = phi->getNext())
            {
                if (dynamic_cast<PhiInstruction *>(phi)->get_incomplete() && IncomingVals.count(dynamic_cast<PhiInstruction *>(phi)->getAddr()))
                    dynamic_cast<PhiInstruction *>(phi)->addEdge(BB, IncomingVals[dynamic_cast<PhiInstruction *>(phi)->getAddr()]);
            }
        }
    }
    for (auto inst : freeInsts)
        delete inst;
    freeInsts.clear();
    SimplifyCFG sc(func->getParent());
    for (auto bb : func->getBlockList())
        for (auto phi = bb->begin(); phi != bb->end() && phi->isPHI(); phi = phi->getNext())
            dynamic_cast<PhiInstruction *>(phi)->get_incomplete() = false;
    sc.pass(func);
}