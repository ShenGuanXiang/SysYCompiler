#include "GraphColor.h"
#include "LiveVariableAnalysis.h"
#include "LinearScan.h"
#include "LoopUnroll.h"
#include <math.h>
#include <algorithm>
#include <stack>

// static const double __DBL_MAX__ = 10000000000;

// TODO：加if-else深度、调loop_depth

static bool debug1 = 1;

static std::set<MachineInstruction *> freeInsts; // def not use & coalesced mov insts

static bool is_float = true;

static bool isRightType(MachineOperand *op)
{
    return ((op->getValType()->isInt() && !is_float) || (op->getValType()->isFloat() && is_float));
}

// real reg -> 0 ~ nreg
static inline int Reg2WebIdx(int Reg)
{
    if (is_float)
    {
        return 4 <= Reg && Reg <= 31 ? Reg - 4 : -1;
    }
    return Reg == 14 ? 13 : Reg <= 12 ? Reg
                                      : -1;
}

// 0 ~ nreg -> real reg
static inline int WebIdx2Reg(int Idx)
{
    if (is_float)
    {
        return Idx + 4;
    }
    return Idx == 13 ? 14 : Idx;
}

static inline bool isInterestingReg(MachineOperand *op)
{
    return op->isReg() && isRightType(op) && Reg2WebIdx(op->getReg()) != -1;
}

static bool isImmWeb(Web *web)
{
    if (web->defs.size() != 1 || web->uses.size() == 1)
        return false;
    auto minst = (*web->defs.begin())->getParent();
    return (minst->isMov() || minst->isLoad()) && minst->getUse().size() == 1 && minst->getUse()[0]->isImm() && minst->getUse()[0]->getValType()->isInt();
}

RegisterAllocation::RegisterAllocation(MachineUnit *unit)
{
    this->unit = unit;
    nregs = 28;
}

void RegisterAllocation::pass()
{
    for (auto &f : unit->getFuncs())
    {
        func = f;
        for (int i = 0; i < 2; i++)
        {
            bool success = false;
            while (!success)
            {
                bool change = true;
                while (change)
                {
                    // 发现du链中所有的网
                    makeWebs();
                    if (debug1)
                        fprintf(stderr, "makeWebs\n");
                    // 构造冲突矩阵
                    buildAdjMatrix();
                    if (debug1)
                        fprintf(stderr, "buildAdjMatrix\n");
                    // 寄存器合并
                    change = regCoalesce();
                    if (debug1)
                        fprintf(stderr, "regCoalesce\n");
                }
                // 构造邻接表
                buildAdjLists();
                if (debug1)
                    fprintf(stderr, "buildAdjLists\n");
                // 计算符号寄存器溢出到内存和从内存恢复的开销
                computeSpillCosts();
                if (debug1)
                    fprintf(stderr, "computeSpillCosts\n");
                if (debug1)
                    for (int i = nregs; i < webs.size(); i++)
                        webs[i]->Print();
                //
                pruneGraph();
                if (debug1)
                    fprintf(stderr, "pruneGraph\n");
                // 着色
                success = assignRegs();
                if (debug1)
                    fprintf(stderr, "assignRegs\n");
                if (debug1)
                    for (int i = nregs; i < webs.size(); i++)
                        webs[i]->Print();
                if (success)
                    // 将颜色替换为真实寄存器
                    modifyCode();
                else
                {
                    // 产生溢出指令
                    genSpillCode();
                    if (debug1)
                        fprintf(stderr, "genSpillCode\n");
                }
                if (debug1)
                    for (int i = nregs; i < webs.size(); i++)
                        webs[i]->Print();
            }
            nregs = nregs == 14 ? 28 : 14;
            is_float ^= 1;
        }
        if (debug1)
            fprintf(stderr, "\n pass %s\n", dynamic_cast<IdentifierSymbolEntry *>(f->getSymPtr())->getName().c_str());
    }

    for (auto inst : freeInsts)
        delete inst;
    freeInsts.clear();
    for (auto web : webs)
        delete web;
    webs.clear();
}

void RegisterAllocation::makeDuChains()
{
    bool change;
    do
    {
        change = false;
        func->AnalyzeLiveVariable();
        std::map<MachineOperand, std::set<MachineOperand *>> all_uses = func->getAllUses();
        du_chains.clear();
        std::map<MachineOperand, std::set<MachineOperand *>> liveVar;
        for (auto bb : func->getBlocks())
        {
            liveVar.clear();
            for (auto &t : bb->getLiveOut())
            {
                if ((t->isVReg() || isInterestingReg(t)) && isRightType(t))
                    liveVar[*t].insert(t);
            }
            for (auto inst = bb->getInsts().rbegin(); inst != bb->getInsts().rend(); inst++)
            {
                for (auto &def : (*inst)->getDef())
                {
                    if ((def->isVReg() || isInterestingReg(def)) && isRightType(def))
                    {
                        auto &uses = liveVar[*def];
                        du_chains[*def].insert({{def}, std::set<MachineOperand *>(uses.begin(), uses.end())});
                        if ((*inst)->getCond() == MachineInstruction::NONE) // TODO
                        {
                            auto &kill = all_uses[*def];
                            std::set<MachineOperand *> res;
                            set_difference(uses.begin(), uses.end(), kill.begin(), kill.end(), inserter(res, res.end()));
                            liveVar[*def] = res;
                        }
                    }
                }
                for (auto &use : (*inst)->getUse())
                {
                    if ((use->isVReg() || isInterestingReg(use)) && isRightType(use))
                        liveVar[*use].insert(use);
                }
            }
        }
        //*****************************************************************************
        // 删除未使用的虚拟寄存器定义
        for (auto &du_chain : du_chains)
        {
            auto du_iter = du_chain.second.begin();
            while (du_iter != du_chain.second.end())
            {
                auto du = *du_iter;
                assert(du.defs.size() == 1);
                auto def = *du.defs.begin();
                if (du.uses.empty() && // def not use
                    !def->getParent()->isCritical() &&
                    def->getParent()->getDef().size() == 1) // TODO：定义多个的还没删
                {
                    change = true;
                    for (auto &inst_use : def->getParent()->getUse())
                    {
                        if (du_chains.count(*inst_use))
                        {
                            std::vector<RegisterAllocation::DU> v;
                            v.assign(du_chains[*inst_use].begin(), du_chains[*inst_use].end());
                            du_chains[*inst_use].clear();
                            for (auto affected_du_iter = v.begin(); affected_du_iter != v.end(); affected_du_iter++)
                            {
                                auto use_iter = std::find((*affected_du_iter).uses.begin(), (*affected_du_iter).uses.end(), inst_use);
                                if (use_iter != (*affected_du_iter).uses.end())
                                    (*affected_du_iter).uses.erase(use_iter);
                                du_chains[*inst_use].insert(*affected_du_iter);
                            }
                        }
                    }
                    def->getParent()->getParent()->removeInst(def->getParent());
                    freeInsts.insert(def->getParent());
                    du_iter = du_chain.second.erase(du_iter);
                    continue;
                }
                du_iter++;
            }
        }
    } while (change);
    //**********************************************************************************
    // 合并交叉的chain
    for (auto &du_chain : du_chains)
    {
        bool change;
        do
        {
            change = false;
            std::vector<RegisterAllocation::DU> v;
            v.assign(du_chain.second.begin(), du_chain.second.end());
            du_chain.second.clear();
            for (size_t i = 0; i != v.size(); i++)
            {
                if (!v[i].defs.empty())
                {
                    for (size_t j = i + 1; j != v.size(); j++)
                    {
                        if (!v[j].defs.empty() && **v[i].defs.begin() == **v[j].defs.begin())
                        {
                            std::set<MachineOperand *> temp;
                            set_intersection(v[i].uses.begin(), v[i].uses.end(), v[j].uses.begin(), v[j].uses.end(), inserter(temp, temp.end()));
                            if (!temp.empty())
                            {
                                change = true;
                                v[i].defs.insert(v[j].defs.begin(), v[j].defs.end());
                                v[i].uses.insert(v[j].uses.begin(), v[j].uses.end());
                                v[j].defs.clear();
                                v[j].uses.clear();
                            }
                        }
                    }
                    // assert(!v[i].uses.empty());
                    du_chain.second.insert(v[i]);
                }
            }
        } while (change);
    }
}

void RegisterAllocation::makeWebs()
{
    makeDuChains();
    for (auto web : webs)
        delete web;
    webs.clear();
    operand2web.clear();

    for (int i = 0; i < nregs; i++)
    {
        // sreg = -1, rreg = idx2reg, spillCost = INF
        Web *regWeb = new Web({std::set<MachineOperand *>(), std::set<MachineOperand *>(), false, __DBL_MAX__ / 2, -1, -1, WebIdx2Reg(i)});
        webs.push_back(regWeb);
    }

    int idx = nregs;
    for (auto &du_chain : du_chains)
    {
        for (auto &du : du_chain.second)
        {
            if (du_chain.first.isReg())
            {
                for (auto &def : du.defs)
                    operand2web[def] = Reg2WebIdx(def->getReg());
                for (auto &use : du.uses)
                    operand2web[use] = Reg2WebIdx(use->getReg());
            }
            else
            {
                // sreg = idx, rreg = -1, spillCost = 0
                Web *web = new Web({du.defs, du.uses, false, 0, idx, -1, -1});
                webs.push_back(web);
                for (auto &def : web->defs)
                    operand2web[def] = idx;
                for (auto &use : web->uses)
                    operand2web[use] = idx;
                idx++;
            }
        }
    }
}

// build the adjacency matrix representation of the interference graph
void RegisterAllocation::buildAdjMatrix()
{
    func->AnalyzeLiveVariable();

    adjMtx.clear();
    adjMtx.resize(webs.size());
    for (int i = 0; i < (int)webs.size(); i++)
        adjMtx[i].resize(webs.size());

    for (int i = 0; i < (int)webs.size(); i++)
        for (int j = 0; j < (int)webs.size(); j++)
            adjMtx[i][j] = 0;

    for (int i = 0; i < nregs; i++)
    {
        for (int j = 0; j < i; j++)
        {
            adjMtx[i][j] = true;
            adjMtx[j][i] = true;
        }
    }

    for (auto &bb : func->getBlocks())
    {
        auto livenow = bb->getLiveOut();
        std::map<MachineOperand, int> condCnt;
        for (auto inst = bb->getInsts().rbegin(); inst != bb->getInsts().rend(); inst++)
        {
            auto defs = (*inst)->getDef();
            for (auto &def : defs)
            {
                if (operand2web.find(def) != operand2web.end())
                {
                    int u = operand2web[def];
                    for (auto &live : livenow)
                    {
                        if (operand2web.find(live) == operand2web.end())
                            continue;
                        int v = operand2web[live];
                        adjMtx[u][v] = 1;
                        adjMtx[v][u] = 1;
                    }
                }
                bool flag = true;
                if ((*inst)->getCond() != MachineInstruction::NONE)
                {
                    if (du_chains[*def].size() != 1)
                        flag = false;
                    else
                    {
                        auto du = *du_chains[*def].begin();
                        if (du.defs.size() > 1 && (!condCnt.count(**du.defs.begin()) || condCnt[**du.defs.begin()] < du.defs.size() - 1))
                        {
                            flag = false;
                            if (!condCnt.count(**du.defs.begin()))
                                condCnt[**du.defs.begin()] = 1;
                            else
                                condCnt[**du.defs.begin()]++;
                        }
                    }
                }
                if (flag)
                {
                    auto &kill = func->getAllUses()[*def];
                    std::set<MachineOperand *> res;
                    set_difference(livenow.begin(), livenow.end(), kill.begin(), kill.end(), inserter(res, res.end()));
                    livenow = res;
                }
            }
            for (auto &use : (*inst)->getUse())
                livenow.insert(use);
        }
    }
}

// build the adjacency list representation of the inteference graph
void RegisterAllocation::buildAdjLists()
{
    adjList.clear();
    adjList.resize(adjMtx.size(), std::set<int>());

    for (size_t u = 0; u < adjMtx.size(); u++)
        for (size_t v = 0; v < u; v++)
        {
            if (adjMtx[u][v])
            {
                adjList[u].insert(v);
                adjList[v].insert(u);
            }
        }
    rmvList = adjList;
}

void RegisterAllocation::computeSpillCosts()
{
    // MLoopAnalyzer mla;
    // mla.FindLoops(func);
    // std::map<MachineBlock *, int> loop_depth;
    // for (auto bb : func->getBlocks())
    // {
    //     loop_depth[bb] = 0;
    // }
    // for (auto loop : mla.getLoops())
    // {
    //     for (auto bb : loop->GetLoop()->GetBasicBlock())
    //     {
    //         loop_depth[bb] = std::max(loop->GetLoop()->GetDepth(), loop_depth[bb]);
    //     }
    // }

    int inst_no = 0;

    for (auto &bb : func->getBlocks())
    {
        // double factor = pow(4, loop_depth[bb]);
        // double factor = 10 * loop_depth[bb];
        double factor = 1.f;
        for (auto &inst : bb->getInsts())
        {
            inst->setNo(inst_no++);
            auto defs = inst->getDef();
            for (auto &def : defs)
            {
                if (!operand2web.count(def))
                    continue;
                int w = operand2web[def];

                if (isImmWeb(webs[w]))
                    webs[w]->spillCost -= factor * adjList[w].size();

                webs[w]->spillCost += factor * 20;

                if (inst->isMov())
                    webs[w]->spillCost -= factor * 10;
            }
            for (auto &use : inst->getUse())
            {
                if (!operand2web.count(use))
                    continue;
                int w = operand2web[use];
                webs[w]->spillCost += factor * 40;

                if (inst->isMov())
                    webs[w]->spillCost -= factor * 10;
            }
        }
    }

    for (int i = nregs; i < (int)webs.size(); i++)
    {
        auto web = webs[i];
        bool short_live_range = true;
        auto bb = (*web->defs.begin())->getParent()->getParent();
        for (auto def : web->defs)
        {
            if (def->getParent()->getParent() != bb || abs(def->getParent()->getNo() - (*web->defs.begin())->getParent()->getNo()) > 5)
            {
                short_live_range = false;
                break;
            }
        }
        for (auto use : web->uses)
        {
            if (use->getParent()->getParent() != bb || abs(use->getParent()->getNo() - (*web->defs.begin())->getParent()->getNo()) > 5)
            {
                short_live_range = false;
                break;
            }
        }

        if (short_live_range)
            webs[i]->spillCost = __DBL_MAX__ / 4;
    }
}

void RegisterAllocation::adjustIG(int i)
{
    for (auto v : adjList[i])
    {
        adjList[v].erase(i);
        if (adjList[v].empty())
            pruneStack.push_back(v);
    }
    adjList[i].clear();
}

void RegisterAllocation::pruneGraph()
{
    pruneStack.clear();
    for (size_t i = nregs; i < adjList.size(); i++)
        if (adjList[i].size() == 0)
            pruneStack.push_back(i);
    bool success = true;
    while (success)
    {
        success = false;
        for (size_t i = nregs; i < adjList.size(); i++)
        {
            if (adjList[i].size() > 0 && adjList[i].size() < (size_t)nregs)
            {
                success = true;
                pruneStack.push_back(i);
                adjustIG(i);
            }
        }
    }
    while (pruneStack.size() < adjList.size() - nregs)
    {
        double minSpillCost = __DBL_MAX__;
        int spillnode = -1;
        for (size_t i = nregs; i < adjList.size(); i++)
        {
            int deg = adjList[i].size();
            if (deg == 0)
                continue;
            if (webs[i]->spillCost / (deg * deg) < minSpillCost)
            {
                minSpillCost = webs[i]->spillCost / (deg * deg);
                spillnode = i;
            }
        }
        assert(spillnode != -1);
        pruneStack.push_back(spillnode);
        adjustIG(spillnode);
    }
}

bool RegisterAllocation::regCoalesce()
{
    bool flag = false;
    std::vector<MachineInstruction *> del_list;
    for (auto &bb : func->getBlocks())
    {
        for (auto &inst : bb->getInsts())
        {
            if (!(inst->isMov() || inst->isVmov() || inst->isZext()) || inst->getUse().size() != 1 || inst->getDef().size() != 1 || inst->getCond() != MachineInstruction::NONE)
                continue;
            MachineOperand *dst = *inst->getDef().begin();
            MachineOperand *src = *inst->getUse().begin();
            if (!((dst->getValType()->isInt() && src->getValType()->isInt()) || (dst->getValType()->isFloat() && src->getValType()->isFloat())))
                continue;
            if (operand2web.find(dst) == operand2web.end())
                continue;
            if (operand2web.find(src) == operand2web.end())
                continue;
            if (dst->isReg() || (src->isReg() && !isInterestingReg(src)))
                continue;
            int u = operand2web[dst];
            int v = operand2web[src];
            if (webs[u]->defs.size() > 1)
                continue;
            if (!adjMtx[u][v])
            {
                flag = true;
                for (size_t i = 0; i < adjMtx.size(); i++)
                    adjMtx[i][u] = adjMtx[i][v] = adjMtx[v][i] = adjMtx[u][i] = adjMtx[u][i] || adjMtx[v][i];
                for (auto &use : webs[u]->uses)
                {
                    MachineInstruction *parent = use->getParent();
                    MachineOperand *new_version = new MachineOperand(*src);
                    operand2web[new_version] = v;
                    webs[v]->uses.insert(new_version);
                    for (size_t i = 0; i < parent->getUse().size(); i++)
                    {
                        if (parent->getUse()[i] == use)
                        {
                            parent->getUse()[i] = new_version;
                            parent->getUse()[i]->setParent(parent);
                        }
                    }
                }
                for (auto &def : webs[u]->defs)
                {
                    MachineInstruction *parent = def->getParent();
                    MachineOperand *new_version = new MachineOperand(*src);
                    operand2web[new_version] = v;
                    webs[v]->defs.insert(new_version);
                    for (size_t i = 0; i < parent->getDef().size(); i++)
                    {
                        if (parent->getDef()[i] == def)
                        {
                            parent->getDef()[i] = new_version;
                            parent->getDef()[i]->setParent(parent);
                        }
                    }
                }
                del_list.push_back(inst);
            }
        }
    }
    for (auto &inst : del_list)
    {
        delete inst;
    }
    return flag;
}

bool RegisterAllocation::assignRegs()
{
    bool success;
    success = true;
    while (!pruneStack.empty())
    {
        int w = pruneStack.back();
        pruneStack.pop_back();
        int color = minColor(w);
        if (color < 0)
        {
            assert(webs[w]->rreg == -1);
            success = false;
            webs[w]->spill = true;
        }
        else
        {
            if (webs[w]->rreg != -1)
                assert(webs[w]->rreg == color);
            webs[w]->rreg = color;
        }
    }
    return success;
}

void RegisterAllocation::modifyCode()
{
    for (int i = nregs; i < (int)webs.size(); i++)
    {
        Web *web = webs[i];
        if ((*(web->defs.begin()))->isReg())
            continue;

        for (auto def : web->defs)
            def->setReg(web->rreg);
        for (auto use : web->uses)
            use->setReg(web->rreg);

        bool save = true;
        auto func_se = dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr());
        for (auto [reg, tp] : func_se->getOccupiedRegs())
        {
            if (reg == web->rreg && ((tp->isFloat() && is_float) || (tp->isInt() && !is_float)))
            {
                save = false;
                break;
            }
        }
        if (web->rreg < 4)
        {
            for (auto param : func_se->getParamsSe())
            {
                if (param->getParamNo() == web->rreg && ((is_float && param->getType()->isFloat()) || (!is_float && (param->getType()->isInt() || param->getType()->isPTR()))))
                {
                    save = false;
                    break;
                }
            }
        }
        auto retType = dynamic_cast<FunctionType *>(func_se->getType())->getRetType();
        if (web->rreg == 0 && ((!is_float && retType->isInt()) || (is_float && retType->isFloat())))
            save = false;
        if (save)
            func->addSavedRegs(web->rreg, is_float);
    }
}

void RegisterAllocation::genSpillCode()
{
    for (auto &web : webs)
    {
        if (!web->spill)
            continue;
        if (debug1)
            web->Print();
        // 保存常数的寄存器单独讨论
        if (isImmWeb(web))
        {
            auto imm = (*web->defs.begin())->getParent()->getUse()[0];
            for (auto &use : web->uses)
            {
                auto block = use->getParent()->getParent();
                auto pos = use->getParent();
                if (!pos->isBL() && !pos->isDummy())
                {
                    block->insertBefore(pos, new LoadMInstruction(block, new MachineOperand(*use), new MachineOperand(*imm)));
                }
            }
        }
        else
        {
            web->disp = -func->AllocSpace(4);
            for (auto &use : web->uses)
            {
                auto block = use->getParent()->getParent();
                auto pos = use->getParent();
                if (!pos->isBL() && !pos->isDummy())
                {
                    auto offset = new MachineOperand(MachineOperand::IMM, web->disp);
                    if ((use->getValType()->isInt() && (offset->getVal() < -4095 || offset->getVal() > 4095)) || (use->getValType()->isFloat() && (offset->isIllegalShifterOperand() || offset->getVal() < -1023 || offset->getVal() > 1023)))
                    {
                        auto internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                        auto ldr = new LoadMInstruction(block, internal_reg, offset);
                        block->insertBefore(pos, ldr);
                        offset = new MachineOperand(*internal_reg);
                    }
                    if (use->getValType()->isFloat() && !offset->isImm())
                    {
                        auto internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::intType);
                        block->insertBefore(pos, new BinaryMInstruction(block, BinaryMInstruction::ADD, internal_reg, new MachineOperand(MachineOperand::REG, 11), offset));
                        block->insertBefore(pos, new LoadMInstruction(block, new MachineOperand(*use), new MachineOperand(*internal_reg)));
                    }
                    else
                        block->insertBefore(pos, new LoadMInstruction(block, new MachineOperand(*use), new MachineOperand(MachineOperand::REG, 11), offset));
                }
            }
            for (auto &def : web->defs)
            {
                auto block = def->getParent()->getParent();
                auto pos = def->getParent();
                if (!pos->isBL() && !pos->isDummy())
                {
                    auto offset = new MachineOperand(MachineOperand::IMM, web->disp);
                    if ((def->getValType()->isInt() && (offset->getVal() < -4095 || offset->getVal() > 4095)) || (def->getValType()->isFloat() && (offset->isIllegalShifterOperand() || offset->getVal() < -1023 || offset->getVal() > 1023)))
                    {
                        auto internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                        auto ldr = new LoadMInstruction(block, internal_reg, offset);
                        block->insertAfter(pos, ldr);
                        offset = new MachineOperand(*internal_reg);
                        pos = ldr;
                    }
                    if (def->getValType()->isFloat() && !offset->isImm())
                    {
                        auto internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::intType);
                        auto add = new BinaryMInstruction(block, BinaryMInstruction::ADD, internal_reg, new MachineOperand(MachineOperand::REG, 11), offset);
                        block->insertAfter(pos, add);
                        block->insertAfter(add, new StoreMInstruction(block, new MachineOperand(*def), new MachineOperand(*internal_reg)));
                    }
                    else
                        block->insertAfter(pos, new StoreMInstruction(block, new MachineOperand(*def), new MachineOperand(MachineOperand::REG, 11), offset));
                }
            }
        }
    }
}

int RegisterAllocation::minColor(int u)
{
    std::vector<bool> bitv(nregs, 0);
    for (auto &v : rmvList[u])
    {
        if (webs[v]->rreg != -1)
            bitv[Reg2WebIdx(webs[v]->rreg)] = 1;
    }
    for (int i = 0; i < nregs; i++)
        if (bitv[i] == 0)
            return WebIdx2Reg(i);
    return -1;
}
