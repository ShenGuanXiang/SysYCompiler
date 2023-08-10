#include "GraphColor.h"
#include "LiveVariableAnalysis.h"
#include "LinearScan.h"
#include "LoopUnroll.h"
#include <math.h>
#include <algorithm>
#include <stack>

// static const double __DBL_MAX__ = 10000000000;

static bool debug1 = 0;

static std::set<MachineInstruction *> freeInsts; // def not use & coalesced mov insts

// real reg -> 0 ~ nreg
static int Reg2WebIdx(int Reg)
{
    return Reg == 14 ? 13 : Reg <= 12 ? Reg
                                      : -1;
}

// 0 ~ nreg -> real reg
static int WebIdx2Reg(int Idx)
{
    return Idx == 13 ? 14 : Idx;
}

static bool isInterestingReg(MachineOperand *op)
{
    return op->isReg() && op->getValType()->isInt() && Reg2WebIdx(op->getReg()) != -1;
}

static bool isImmWeb(Web *web)
{
    if (web->defs.size() != 1 || web->uses.size() == 1)
        return false;
    auto minst = (*web->defs.begin())->getParent();
    return (minst->isMov() || minst->isLoad()) && minst->getUse().size() == 1 && minst->getUse()[0]->isImm();
}

RegisterAllocation::RegisterAllocation(MachineUnit *unit)
{
    this->unit = unit;
    nregs = 14;
}

void RegisterAllocation::pass()
{
    for (auto &f : unit->getFuncs())
    {
        func = f;
        bool success = false;
        while (!success)
        {
            bool change = true;
            while (change)
            {
                // 发现du链中所有的网
                if (makeWebs() == false)
                    return;
                if (debug1)
                    fprintf(stderr, "makeWebs\n");
                // 构造冲突矩阵
                buildAdjMatrix();
                if (debug1)
                    fprintf(stderr, "buildAdjMatrix\n");
                // 寄存器合并
                // change = regCoalesce();
                change = false;
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
            {
                for (int i = nregs; i < webs.size(); i++)
                {
                    webs[i]->Print();
                }
            }
            if (debug1)
                fprintf(stderr, "computeSpillCosts\n");
            //
            pruneGraph();
            if (debug1)
                fprintf(stderr, "pruneGraph\n");
            // 着色
            success = assignRegs();
            if (debug1)
                fprintf(stderr, "assignRegs\n");
            if (debug1)
            {
                for (int i = nregs; i < webs.size(); i++)
                {
                    webs[i]->Print();
                }
            }
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
            {
                for (int i = nregs; i < webs.size(); i++)
                {
                    webs[i]->Print();
                }
            }
        }
    }
    if (debug1)
        fprintf(stderr, "\n pass\n");

    for (auto inst : freeInsts)
        delete inst;
    freeInsts.clear();
}

bool RegisterAllocation::makeDuChains()
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
                if (t->getValType()->isInt() && (t->isVReg() || isInterestingReg(t)))
                    liveVar[*t].insert(t);
            }
            for (auto inst = bb->getInsts().rbegin(); inst != bb->getInsts().rend(); inst++)
            {
                for (auto &def : (*inst)->getDef())
                {
                    if (def->getValType()->isInt() && (def->isVReg() || isInterestingReg(def)))
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
                    if (def->isReg() && def->getValType()->isFloat())
                        return false;
                }
                for (auto &use : (*inst)->getUse())
                {
                    if (use->getValType()->isInt() && (use->isVReg() || isInterestingReg(use)))
                        liveVar[*use].insert(use);
                    if (use->isReg() && use->getValType()->isFloat())
                        return false;
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
        //*****************************************************************************
        // coalesce： 删除一些mov，但会延长生命期
        for (auto &du_chain : du_chains)
        {
            auto du_chain_second_copy = du_chain.second;
            auto du_iter = du_chain_second_copy.begin();
            while (du_iter != du_chain_second_copy.end())
            {
                auto du = *du_iter;
                assert(du.defs.size() == 1);
                auto def = *du.defs.begin();
                if ((def->getParent()->isMov() || def->getParent()->isVmov() || def->getParent()->isZext()) &&
                    def->getParent()->getUse().size() == 1 && def->getParent()->getCond() == MachineInstruction::NONE)
                {
                    auto src = def->getParent()->getUse()[0];
                    if ((def->isVReg() && (src->isVReg() /*|| isInterestingReg(src)*/) &&
                         (def->getValType()->isInt() && src->getValType()->isInt())) ||
                        *def == *src)
                    {
                        assert(du_chains.count(*src));
                        if (def->getParent()->isZext())
                            assert(du_chains[*src].size() == 2);
                        else if (du_chains[*def].size() > 1 || du_chains[*src].size() > 1)
                        {
                            bool eliminable = true;
                            for (auto &du : du_chains[*def])
                                if ((*du.defs.begin())->getParent()->getParent() != (*(*du_chains[*def].begin()).defs.begin())->getParent()->getParent() || (*du.defs.begin())->getParent()->getCond() != MachineInstruction::NONE)
                                {
                                    eliminable = false;
                                    break;
                                }
                            for (auto &du : du_chains[*src])
                                if ((*du.defs.begin())->getParent()->getParent() != (*(*du_chains[*src].begin()).defs.begin())->getParent()->getParent() || (*du.defs.begin())->getParent()->getCond() != MachineInstruction::NONE)
                                {
                                    eliminable = false;
                                    break;
                                }
                            if (!eliminable)
                            {
                                du_iter++;
                                continue;
                            }
                            else
                            {
                                std::vector<MachineOperand *> oldOps, newOps;
                                for (auto &inst : def->getParent()->getParent()->getInsts())
                                {
                                    if (inst->getNo() <= def->getParent()->getNo())
                                        continue;
                                    bool redef = false;
                                    for (auto it = inst->getUse().begin(); it != inst->getUse().end(); it++)
                                        if (**it == *def)
                                        {
                                            oldOps.push_back(*it);
                                            *it = new MachineOperand(*src);
                                            (*it)->setParent(inst);
                                            newOps.push_back(*it);
                                        }
                                    for (auto inst_def : inst->getDef())
                                        if (*inst_def == *def || *inst_def == *src)
                                            redef = true;
                                    if (redef)
                                        break;
                                }
                                if (!oldOps.empty() && !(*def == *src))
                                    change = true;
                                if (oldOps.size() < du.uses.size())
                                {
                                    std::vector<RegisterAllocation::DU> v_def;
                                    v_def.assign(du_chains[*def].begin(), du_chains[*def].end());
                                    du_chains[*def].clear();
                                    for (auto oldOp : oldOps)
                                    {
                                        int cnt = 0;
                                        for (size_t i = 0; i != v_def.size(); i++)
                                        {
                                            if (v_def[i].uses.find(oldOp) != v_def[i].uses.end())
                                            {
                                                assert(cnt == 0);
                                                cnt++;
                                                v_def[i].uses.erase(oldOp);
                                            }
                                        }
                                    }
                                    for (size_t i = 0; i != v_def.size(); i++)
                                        du_chains[*def].insert(v_def[i]);
                                }
                                else
                                {
                                    assert(oldOps.size() == du.uses.size());
                                    def->getParent()->getParent()->removeInst(def->getParent());
                                    freeInsts.insert(def->getParent());
                                    du_chain.second.erase(du);
                                }
                                std::vector<RegisterAllocation::DU> v_src;
                                v_src.assign(du_chains[*src].begin(), du_chains[*src].end());
                                du_chains[*src].clear();
                                int cnt = 0;
                                for (size_t i = 0; i != v_src.size(); i++)
                                {
                                    if (v_src[i].uses.find(src) != v_src[i].uses.end())
                                    {
                                        assert(cnt == 0);
                                        cnt++;
                                        for (auto newOp : newOps)
                                            v_src[i].uses.insert(newOp);
                                    }
                                    du_chains[*src].insert(v_src[i]);
                                }
                                du_iter++;
                                continue;
                            }
                        }
                        change = true;
                        std::vector<MachineOperand *> newOps;
                        for (auto use : du.uses)
                        {
                            auto inst = use->getParent();
                            for (auto iter = inst->getUse().begin(); iter != inst->getUse().end(); iter++)
                            {
                                if (*iter == use)
                                {
                                    *iter = new MachineOperand(*src);
                                    (*iter)->setParent(inst);
                                    newOps.push_back(*iter);
                                }
                            }
                        }
                        std::vector<RegisterAllocation::DU> v;
                        v.assign(du_chains[*src].begin(), du_chains[*src].end());
                        du_chains[*src].clear();
                        for (size_t i = 0; i != v.size(); i++)
                        {
                            if (v[i].uses.find(src) != v[i].uses.end())
                            {
                                for (auto newOp : newOps)
                                    v[i].uses.insert(newOp);
                                v[i].uses.erase(src);
                            }
                            du_chains[*src].insert(v[i]);
                        }
                        def->getParent()->getParent()->removeInst(def->getParent());
                        freeInsts.insert(def->getParent());
                        du_chain.second.erase(du);
                        du_iter++;
                        continue;
                    }
                }
                du_iter++;
            }
        }
    } while (change);
    //**********************************************************************************
    // 合并相同MachineOperand的多个def
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
    return true;
}

bool RegisterAllocation::makeWebs()
{
    if (makeDuChains() == false)
        return false;
    webs.clear();
    operand2web.clear();
    for (auto &du_chain : du_chains)
    {
        for (auto &du : du_chain.second)
        {
            Web *web;
            double initSpillCost = 0;
            if (du_chain.first.isReg())
            {
                initSpillCost = __DBL_MAX__ / 2;
            }
            // sreg = -1, rreg = -1
            web = new Web({du.defs, du.uses, false, initSpillCost, -1, -1, -1});
            webs.push_back(web);
        }
    }
    int idx = nregs;
    for (auto &web : webs)
    {
        web->sreg = idx;
        for (auto &def : web->defs)
            operand2web[def] = idx;
        for (auto &use : web->uses)
            operand2web[use] = idx;
        idx++;
    }

    std::vector<Web *> rregWebs;
    for (int i = 0; i < nregs; i++)
    {
        // sreg = -1, rreg = idx2reg
        Web *reg = new Web({std::set<MachineOperand *>(), std::set<MachineOperand *>(), false, __DBL_MAX__ / 2, -1, -1, WebIdx2Reg(i)});
        rregWebs.push_back(reg);
    }

    webs.insert(webs.begin(), rregWebs.begin(), rregWebs.end());
    return true;
}

// build the adjacency matrix representation of the interference graph
void RegisterAllocation::buildAdjMatrix()
{
    func->AnalyzeLiveVariable();

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

    // pre-allocation
    for (int i = nregs; i < (int)webs.size(); i++)
    {
        MachineOperand *def = *webs[i]->defs.begin();
        if (!def->isReg())
            continue;
        assert(isInterestingReg(def));
        webs[i]->rreg = def->getReg();
        int u = operand2web[def];
        for (int j = 0; j < nregs; j++)
        {
            if (j == Reg2WebIdx(webs[i]->rreg))
                continue;
            adjMtx[u][j] = 1;
            adjMtx[j][u] = 1;
        }
    }

    for (auto &bb : func->getBlocks())
    {
        auto livenow = bb->getLiveOut();
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
                if ((*inst)->getCond() == MachineInstruction::NONE)
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

    for (auto &bb : func->getBlocks())
    {
        // double factor = pow(4, loop_depth[bb]); // TODO：加if-else深度、调loop_depth
        // double factor = 10 * loop_depth[bb];
        double factor = 1.f;
        for (auto &inst : bb->getInsts())
        {
            auto defs = inst->getDef();
            for (auto &def : defs)
            {
                if (!operand2web.count(def))
                    continue;
                int w = operand2web[def];

                if (isImmWeb(webs[w]))
                    webs[w]->spillCost -= factor * 200;

                webs[w]->spillCost += factor * 2;

                if (inst->isMov())
                    webs[w]->spillCost -= factor;
            }
            for (auto &use : inst->getUse())
            {
                if (!operand2web.count(use))
                    continue;
                int w = operand2web[use];
                webs[w]->spillCost += factor * 4;

                if (inst->isMov())
                    webs[w]->spillCost -= factor;
            }
        }
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
    for (size_t i = 0; i < adjList.size(); i++)
        if (adjList[i].size() == 0)
            pruneStack.push_back(i);
    bool success = true;
    while (success)
    {
        success = false;
        for (size_t i = 0; i < adjList.size(); i++)
        {
            if (adjList[i].size() > 0 && adjList[i].size() < (size_t)nregs && webs[i]->rreg == -1)
            {
                success = true;
                pruneStack.push_back(i);
                adjustIG(i);
            }
        }
    }
    while (pruneStack.size() < adjList.size())
    {
        double minSpillCost = __DBL_MAX__;
        int spillnode = -1;
        for (size_t i = 0; i < adjList.size(); i++)
        {
            int deg = adjList[i].size();
            if (deg == 0)
                continue;
            if (webs[i]->spillCost / deg < minSpillCost)
            {
                minSpillCost = webs[i]->spillCost / deg;
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
            if (!(inst->isMov() || inst->isVmov() || inst->isZext()) || inst->isMovShift() || inst->getCond() != MachineInstruction::NONE)
                continue;
            MachineOperand *dst = *inst->getDef().begin();
            MachineOperand *src = *inst->getUse().begin();
            if (!((dst->getValType()->isInt() && src->getValType()->isInt()) || (dst->getValType()->isFloat() && src->getValType()->isFloat())))
                continue;
            if (operand2web.find(dst) == operand2web.end())
                continue;
            if (operand2web.find(src) == operand2web.end())
                continue;
            if ((dst->isReg() && !isInterestingReg(dst)) || (src->isReg() && !isInterestingReg(src)))
                continue;
            int u = operand2web[dst];
            int v = operand2web[src];
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
                    for (int i = 0; i < parent->getUse().size(); i++)
                    {
                        if (*parent->getUse()[i] == *use)
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
                    for (int i = 0; i < parent->getDef().size(); i++)
                    {
                        if (*parent->getDef()[i] == *def)
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
            if (reg == web->rreg && tp->isInt())
            {
                save = false;
                break;
            }
        }
        if (web->rreg < 4)
        {
            for (auto param : func_se->getParamsSe())
            {
                if (param->getParamNo() == web->rreg && (param->getType()->isInt() || param->getType()->isPTR()))
                {
                    save = false;
                    break;
                }
            }
        }
        auto retType = dynamic_cast<FunctionType *>(func_se->getType())->getRetType();
        if (web->rreg == 0 && retType->isInt())
            save = false;
        if (save)
            func->addSavedRegs(web->rreg, false);
    }
}

void RegisterAllocation::genSpillCode()
{
    for (auto &web : webs)
    {
        if (!web->spill)
            continue;
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
