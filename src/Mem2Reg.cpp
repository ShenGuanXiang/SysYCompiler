#include "Mem2Reg.h"
#include "Type.h"
#include <queue>

static std::map<BasicBlock *, std::set<BasicBlock *>> SDoms;
static std::map<Instruction *, unsigned> InstNumbers;
static std::vector<PhiInstruction *> newPHIs;
static std::set<Instruction *> freeList;

static std::map<Operand *, std::set<PhiInstruction *>> defs;
struct AllocaInfo
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

    // 计算哪里的基本块定义(store)和使用(load)了alloc变量
    void AnalyzeAlloca(AllocaInstruction *alloca)
    {
        clear();
        // 获得store指令和load指令所在的基本块，并判断它们是否在同一块中
        for (auto user : alloca->getDef()[0]->getUses())
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
    for (auto func = unit->begin(); func != unit->end(); func++)
    {
        ComputeDom(*func);
        ComputeDomFrontier(*func);
        InsertPhi(*func);
        Rename(*func);
    }
}

// SDoms & IDom
void Mem2Reg::ComputeDom(Function *func)
{
    // Vertex-removal Algorithm, TODO：采用更高效的算法
    SDoms.clear();
    for (auto bb : func->getBlockList())
        SDoms[bb] = std::set<BasicBlock *>();
    std::set<BasicBlock *> all_bbs(func->getBlockList().begin(), func->getBlockList().end());
    for (auto removed_bb : func->getBlockList())
    {
        std::set<BasicBlock *> visited;
        std::queue<BasicBlock *> q;
        std::map<BasicBlock *, bool> is_visited;
        for (auto bb : func->getBlockList())
            is_visited[bb] = false;
        if (func->getEntry() != removed_bb)
        {
            visited.insert(func->getEntry());
            is_visited[func->getEntry()] = true;
            q.push(func->getEntry());
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
                SDoms[bb].insert(removed_bb); // strictly dominators
        }
    }
    // immediate dominator ：严格支配 bb，且不严格支配任何严格支配 bb 的节点的节点
    IDom.clear();
    for (auto bb : func->getBlockList())
    {
        IDom[bb] = SDoms[bb];
        for (auto sdom : SDoms[bb])
        {
            std::set<BasicBlock *> temp;
            set_difference(IDom[bb].begin(), IDom[bb].end(), SDoms[sdom].begin(), SDoms[sdom].end(), inserter(temp, temp.end()));
            IDom[bb] = temp;
        }
        assert(IDom[bb].size() == 1 || (bb == func->getEntry() && IDom[bb].size() == 0));
    }
    // for (auto kv : IDom)
    // {
    //     fprintf(stderr, "IDom[B%d] = {", kv.first->getNo());
    //     for (auto bb : kv.second)
    //         fprintf(stderr, "B%d, ", bb->getNo());
    //     fprintf(stderr, "}\n");
    // }
}

// ref : Static Single Assignment Book
void Mem2Reg::ComputeDomFrontier(Function *func)
{
    DF.clear();
    for (auto bb : func->getBlockList())
        DF[bb] = std::set<BasicBlock *>();
    std::map<BasicBlock *, bool> is_visited;
    for (auto bb : func->getBlockList())
        is_visited[bb] = false;
    std::queue<BasicBlock *> q;
    q.push(func->getEntry());
    is_visited[func->getEntry()] = true;
    while (!q.empty())
    {
        auto a = q.front();
        q.pop();
        std::vector<BasicBlock *> succs(a->succ_begin(), a->succ_end());
        for (auto b : succs)
        {
            auto x = a;
            while (!SDoms[b].count(x))
            {
                assert(x != func->getEntry());
                DF[x].insert(b);
                x = *(IDom[x].begin());
            }
            if (!is_visited[b])
            {
                is_visited[b] = true;
                q.push(b);
            }
        }
    }
    // for (auto kv : DF)
    // {
    //     fprintf(stderr, "DF[B%d] = {", kv.first->getNo());
    //     for (auto bb : kv.second)
    //         fprintf(stderr, "B%d, ", bb->getNo());
    //     fprintf(stderr, "}\n");
    // }
}

static bool isAllocaPromotable(AllocaInstruction *alloca)
{
    if (dynamic_cast<PointerType *>(alloca->getDef()[0]->getEntry()->getType())->isARRAY())
        return false; // toDo ：数组拟采用sroa、向量化优化
    auto users = alloca->getDef()[0]->getUses();
    for (auto user : users)
    {
        // store:不允许alloc的结果作为store的左操作数；不允许store dst的类型和alloc dst的类型不符
        if (user->isStore())
        {
            assert(user->getUses()[1]->getEntry() != alloca->getDef()[0]->getEntry());
            assert(dynamic_cast<PointerType *>(user->getUses()[0]->getType())->getValType() == dynamic_cast<PointerType *>(alloca->getDef()[0]->getType())->getValType());
            if (user->getUses()[1]->getEntry()->isVariable())
                if (dynamic_cast<IdentifierSymbolEntry *>(user->getUses()[1]->getEntry())->isParam())
                    return false; // todo : 函数参数先不提升
        }
        // load: 不允许load src的类型和alloc dst的类型不符
        else if (user->isLoad())
        {
            assert(dynamic_cast<PointerType *>(user->getUses()[0]->getType())->getValType() == dynamic_cast<PointerType *>(alloca->getDef()[0]->getType())->getValType());
        }
        // else if (user->isGep())
        else
            return false;
    }
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
static bool rewriteSingleStoreAlloca(AllocaInstruction *alloca, AllocaInfo &Info)
{
    StoreInstruction *OnlyStore = Info.OnlyStore;
    bool StoringGlobalVal = OnlyStore->getUses()[1]->getEntry()->isVariable() &&
                            dynamic_cast<IdentifierSymbolEntry *>(OnlyStore->getUses()[1]->getEntry())->isGlobal();
    BasicBlock *StoreBB = OnlyStore->getParent();
    int StoreIndex = -1;

    Info.UsingBlocks.clear();

    auto AllocaUsers = alloca->getDef()[0]->getUses();
    for (auto UserInst : AllocaUsers)
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
            else if (!SDoms[UserInst->getParent()].count(StoreBB))
            {
                Info.UsingBlocks.push_back(UserInst->getParent());
                continue;
            }
        }

        auto ReplVal = OnlyStore->getUses()[1];
        UserInst->replaceAllUsesWith(ReplVal); // 替换掉load的所有user
        InstNumbers.erase(UserInst);
        UserInst->getParent()->remove(UserInst);
        freeList.insert(UserInst); // 删除load指令
    }

    // 有 alloca 支配不到的 User
    if (Info.UsingBlocks.size())
        return false;

    // 移除处理好的store和alloca
    InstNumbers.erase(Info.OnlyStore);
    Info.OnlyStore->getParent()->remove(Info.OnlyStore);
    freeList.insert(Info.OnlyStore);
    alloca->getParent()->remove(alloca);
    freeList.insert(alloca);
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

    auto AllocaUsers = alloca->getDef()[0]->getUses();
    for (auto UserInst : AllocaUsers)
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
            assert(0);
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
        freeList.insert(LoadInst);
    }

    // 删除alloca和所有的store指令
    alloca->getParent()->remove(alloca);
    freeList.insert(alloca);
    for (auto kv : StoresByIndex)
    {
        InstNumbers.erase(kv.second);
        kv.second->getParent()->remove(kv.second);
        freeList.insert(kv.second);
    }
    return true;
}

static bool StoreBeforeLoad(BasicBlock *BB, AllocaInstruction *alloca)
{
    for (auto I = BB->begin(); I != BB->end(); I = I->getNext())
        if (I->isLoad() && I->getUses()[0]->getEntry() == alloca->getDef()[0]->getEntry())
            return false;
        else if (I->isStore() && I->getUses()[0]->getEntry() == alloca->getDef()[0]->getEntry())
            return true;
    return false;
}

// 对于一个alloca，检查每一个usingblock，是否在对这个变量load之前有store，有则说明原来的alloca变量被覆盖了，不是live in的
static std::set<BasicBlock *> ComputeLiveInBlocks(AllocaInstruction *alloca, AllocaInfo &Info)
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
    std::queue<BasicBlock *> workList;
    std::map<BasicBlock *, bool> is_visited;
    for (auto bb : alloca->getParent()->getParent()->getBlockList())
        is_visited[bb] = false;
    for (auto bb : LiveInBlocks)
    {
        workList.push(bb);
        is_visited[bb] = true;
    }
    // for (auto bb : DefBlocks) // 到def not use的基本块， live in的迭代终止
    //     is_visited[bb] = true;
    while (!workList.empty())
    {
        auto bb = workList.front();
        workList.pop();
        for (auto pred = bb->pred_begin(); pred != bb->pred_end(); pred++)
            if (!is_visited[*pred] && (DefBlocks.find(*pred) == DefBlocks.end() || !StoreBeforeLoad(*pred, alloca)))
            {
                LiveInBlocks.insert(*pred);
                workList.push(*pred);
                is_visited[*pred] = true;
            }
    }
    return LiveInBlocks;
}

// 将局部变量由内存提升到寄存器的主体函数
void Mem2Reg::InsertPhi(Function *func)
{
    AllocaInfo Info;
    for (auto inst = func->getEntry()->begin(); inst != func->getEntry()->end(); inst = inst->getNext())
    {
        if (!inst->isAlloca())
            continue;
        auto alloca = dynamic_cast<AllocaInstruction *>(inst);
        if (!isAllocaPromotable(alloca))
            continue;
        assert(alloca->getDef()[0]->getType()->isPTR());

        // 筛1：如果alloca出的空间从未被使用，直接删除
        if (alloca->getDef()[0]->getUses().size() == 0)
        {
            alloca->getParent()->remove(alloca);
            freeList.insert(alloca);
            continue;
        }

        // 计算哪里的基本块定义(store)和使用(load)了alloc变量
        Info.AnalyzeAlloca(alloca);

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
        std::queue<BasicBlock *> workList;
        for (auto bb : Info.DefiningBlocks)
            workList.push(bb);
        while (!workList.empty())
        {
            auto bb = workList.front();
            workList.pop();
            for (auto df : DF[bb])
                if (!is_visited[df])
                {
                    is_visited[df] = true;
                    if (LiveInBlocks.find(df) != LiveInBlocks.end())
                    {
                        auto phi = new PhiInstruction(alloca->getDef()[0]); // 现在PHI的dst是PTR
                        df->insertFront(phi);
                        newPHIs.push_back(phi);
                    }
                    is_visited[df] = true;
                    workList.push(df);
                }
        }
    }
}

// 删除源操作数均相同的PHI
static void SimplifyInstruction()
{
    for (auto phi : newPHIs)
    {
        auto srcs = phi->getUses();
        auto last_src = srcs[0];
        bool Elim = true;
        for (auto src : srcs)
        {
            if (src != last_src)
            {
                Elim = false;
                break;
            }
        }
        if (Elim)
        {
            phi->replaceAllUsesWith(last_src);
            phi->getParent()->remove(phi);
            freeList.insert(phi);
        }
    }
}

// https://roife.github.io/2022/02/07/mem2reg/
void Mem2Reg::Rename(Function *func)
{
    // std::map<BasicBlock *, std::set<BasicBlock *>> DT_succ; // DominatorTree
    // // 将IDom反向得到DominatorTree
    // for (auto kv : IDom)
    //     DT_succ[*kv.second.begin()].insert(kv.first);

    using RenamePassData = std::pair<BasicBlock *, std::map<Operand *, Operand *>>; //(bb, addr2val)

    // bfs
    std::map<BasicBlock *, bool> isVisited;
    for (auto bb : func->getBlockList())
        isVisited[bb] = false;
    std::queue<RenamePassData> workList;
    workList.push(std::make_pair(func->getEntry(), std::map<Operand *, Operand *>()));
    while (!workList.empty())
    {
        auto BB = workList.front().first;
        auto IncomingVals = workList.front().second;
        workList.pop();
        if (isVisited[BB])
            continue;
        isVisited[BB] = true;
        for (auto inst = BB->begin(); inst != BB->end(); inst = inst->getNext())
        {
            if (inst->isAlloca() && isAllocaPromotable(dynamic_cast<AllocaInstruction *>(inst)))
            {
                inst->getParent()->remove(inst);
                freeList.insert(inst);
            }
            else if (inst->isLoad())
            {
                if (inst->getUses()[0]->getDef() && inst->getUses()[0]->getDef()->isAlloca() &&
                    isAllocaPromotable(dynamic_cast<AllocaInstruction *>(inst->getUses()[0]->getDef())))
                {
                    inst->replaceAllUsesWith(IncomingVals[inst->getUses()[0]]);
                    inst->getParent()->remove(inst);
                    freeList.insert(inst);
                }
            }
            else if (inst->isStore())
            {
                if (inst->getUses()[0]->getDef() && inst->getUses()[0]->getDef()->isAlloca() &&
                    isAllocaPromotable(dynamic_cast<AllocaInstruction *>(inst->getUses()[0]->getDef())))
                {
                    IncomingVals[inst->getUses()[0]] = inst->getUses()[1];
                    inst->getParent()->remove(inst);
                    freeList.insert(inst);
                }
            }
            else if (inst->isPHI())
            {
                assert(inst->getDef()[0]->getEntry()->getType()->isPTR());
                auto new_dst = new Operand(new TemporarySymbolEntry(dynamic_cast<PointerType *>(inst->getDef()[0]->getType())->getValType(),
                                                                    /*dynamic_cast<TemporarySymbolEntry *>(inst->getDef()[0]->getEntry())->getLabel()*/
                                                                    SymbolTable::getLabel()));
                IncomingVals[inst->getDef()[0]] = new_dst;
                dynamic_cast<PhiInstruction *>(inst)->updateDst(new_dst); // i32*->i32
            }
        }
        for (auto succ = BB->succ_begin(); succ != BB->succ_end(); succ++)
        // for (auto succ : DT_succ[BB])
        {
            // workList.push(std::make_pair(succ, IncomingVals));
            // for (auto phi = succ->begin(); phi != succ->end() && phi->isPHI(); phi = phi->getNext())
            // {
            //     if (IncomingVals.count(dynamic_cast<PhiInstruction *>(phi)->getAddr()))
            //         dynamic_cast<PhiInstruction *>(phi)->addEdge(BB, IncomingVals[dynamic_cast<PhiInstruction *>(phi)->getAddr()]);
            // }
            workList.push(std::make_pair(*succ, IncomingVals));
            for (auto phi = (*succ)->begin(); phi != (*succ)->end() && phi->isPHI(); phi = phi->getNext())
            {
                if (IncomingVals.count(dynamic_cast<PhiInstruction *>(phi)->getAddr()))
                    dynamic_cast<PhiInstruction *>(phi)->addEdge(BB, IncomingVals[dynamic_cast<PhiInstruction *>(phi)->getAddr()]);
            }
        }
    }
    SimplifyInstruction();
    // bug
    // for (auto inst : freeList)
    //     delete inst;
}