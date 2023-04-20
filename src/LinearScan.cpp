#include <algorithm>
#include <stack>
#include "LinearScan.h"
#include "MachineCode.h"
#include "LiveVariableAnalysis.h"

static std::set<MachineInstruction *> freeInsts; // def not use & coalesced mov insts
static std::set<LinearScan::Interval *> freeIntervals;

LinearScan::LinearScan(MachineUnit *unit)
{
    this->unit = unit;
}

static auto compareEnd = [](LinearScan::Interval *a, LinearScan::Interval *b) -> bool
{
    return a->end < b->end;
};
static auto compareStart = [](LinearScan::Interval *a, LinearScan::Interval *b) -> bool
{
    return a->start < b->start;
};

void LinearScan::pass()
{
    for (auto &f : unit->getFuncs())
    {
        func = f;
        bool success;
        do // repeat until all vregs can be mapped
        {
            releaseAllRegs();
            computeLiveIntervals();
            success = linearScanRegisterAllocation();
            if (success) // all vregs can be mapped to real regs
                modifyCode();
            else // spill vregs that can't be mapped to real regs
                genSpillCode();
        } while (!success);
    }
    for (auto inst : freeInsts)
        delete inst;
    freeInsts.clear();
    for (auto interval : intervals)
        freeIntervals.insert(interval);
    intervals.clear();
    active.clear();
    for (auto interval : regIntervals)
        freeIntervals.insert(interval);
    regIntervals.clear();
    for (auto interval : freeIntervals)
        delete interval;
    freeIntervals.clear();
}

void LinearScan::releaseAllRegs()
{
    rregs.clear();
    sregs.clear();

    rregs.push_back(14);
    for (int i = 12; i >= 0; i--) // todo：r12现在还叫不准
        rregs.push_back(i);
    for (int i = 31; i >= 4; i--) // 参数也放进去容易导致 'non-contiguous register range -- vpush/vpop'
        sregs.push_back(i);
    // for (int i = 10; i >= 4; i--)
    //     rregs.push_back(i);
    // for (int i = 31; i >= 16; i--)
    //     sregs.push_back(i);
}

bool LinearScan::isInterestingReg(MachineOperand *reg)
{
    if (reg->isReg())
    {
        if (reg->getValType()->isInt())
        {
            auto it = std::lower_bound(rregs.rbegin(), rregs.rend(), reg->getReg());
            return (it != rregs.rend()) && (*it == reg->getReg());
        }
        else
        {
            assert(reg->getValType()->isFloat());
            auto it = std::lower_bound(sregs.rbegin(), sregs.rend(), reg->getReg());
            return (it != sregs.rend()) && (*it == reg->getReg());
        }
    }
    return false;
}

void LinearScan::makeDuChains()
{
    bool change;
    do
    {
        change = false;
        std::map<MachineOperand, std::set<MachineOperand*>> temp = {};
        func->AnalyzeLiveVariable(temp);
        std::map<MachineOperand, std::set<MachineOperand *>> all_uses;
        for (auto &block : func->getBlocks())
        {
            for (auto &inst : block->getInsts())
            {
                auto uses = inst->getUse();
                for (auto &use : uses)
                {
                    if (use->isVReg() || isInterestingReg(use))
                        all_uses[*use].insert(use);
                }
            }
        }
        du_chains.clear();
        int i = 0;
        std::map<MachineOperand, std::set<MachineOperand *>> liveVar;
        std::map<MachineBlock *, bool> is_visited;
        for (auto bb : func->getBlocks())
            is_visited[bb] = false;
        std::stack<MachineBlock *> st;
        st.push(func->getEntry());
        is_visited[func->getEntry()] = true;
        while (!st.empty())
        {
            auto bb = st.top();
            liveVar.clear();
            for (auto &t : bb->getLiveOut())
            {
                if (t->isVReg() || isInterestingReg(t))
                    liveVar[*t].insert(t);
            }
            int no = i = bb->getInsts().size() + i;
            for (auto inst = bb->getInsts().rbegin(); inst != bb->getInsts().rend(); inst++)
            {
                (*inst)->setNo(no--);
                for (auto &def : (*inst)->getDef())
                {
                    if (def->isVReg() || isInterestingReg(def))
                    {
                        auto &uses = liveVar[*def];
                        du_chains[*def].insert({{def}, std::set<MachineOperand *>(uses.begin(), uses.end())});
                        if ((*inst)->getCond() == MachineInstruction::NONE)
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
                    if (use->isVReg() || isInterestingReg(use))
                        liveVar[*use].insert(use);
                }
            }
            bool next_found = false;
            for (auto succ : bb->getSuccs())
            {
                if (!is_visited[succ])
                {
                    is_visited[succ] = true;
                    st.push(succ);
                    next_found = true;
                    break;
                }
            }
            if (!next_found)
                st.pop();
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
                    !def->getParent()->isBL() && !def->getParent()->isDummy() &&
                    def->getParent()->getDef().size() == 1) // todo：定义多个的还没删
                {
                    change = true;
                    for (auto &inst_use : def->getParent()->getUse())
                    {
                        if (du_chains.count(*inst_use))
                        {
                            std::vector<LinearScan::DU> v;
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
                    if ((def->isVReg() && (src->isVReg() || isInterestingReg(src)) &&
                         ((def->getValType()->isInt() && src->getValType()->isInt()) ||
                          (def->getValType()->isFloat() && src->getValType()->isFloat()))) ||
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
                                    std::vector<LinearScan::DU> v_def;
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
                                std::vector<LinearScan::DU> v_src;
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
                        std::vector<LinearScan::DU> v;
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
            std::vector<LinearScan::DU> v;
            v.assign(du_chain.second.begin(), du_chain.second.end());
            du_chain.second.clear();
            for (size_t i = 0; i != v.size(); i++)
            {
                for (size_t j = i + 1; j != v.size(); j++)
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
                if (!v[i].defs.empty())
                {
                    // assert(!v[i].uses.empty());
                    du_chain.second.insert(v[i]);
                }
            }
        } while (change);
    }
}

void LinearScan::computeLiveIntervals()
{
    makeDuChains();
    for (auto interval : regIntervals)
        freeIntervals.insert(interval);
    regIntervals.clear();
    for (auto interval : intervals)
        freeIntervals.insert(interval);
    intervals.clear();
    for (auto &du_chain : du_chains)
    {
        for (auto &du : du_chain.second)
        {
            int start = 0x7FFFFFFF, end = -1;
            int start_conditional = -1, start_unconditional = 0x7FFFFFFF;
            for (auto &def : du.defs)
            {
                if (def->getParent()->getCond() != MachineInstruction::NONE) // 条件定义的，应该多次定义互相独立
                    start_conditional = std::max(start_conditional, def->getParent()->getNo());
                else
                    start_unconditional = std::min(start_unconditional, def->getParent()->getNo());
            }
            assert(start_conditional == -1 || start_unconditional == 0x7FFFFFFF);
            start = start_conditional == -1 ? start_unconditional : std::min(start_conditional, start_unconditional);
            assert(start != 0x7FFFFFFF);
            for (auto &use : du.uses)
                end = std::max(end, use->getParent()->getNo());
            if (end == -1)
            {
                assert((*du.defs.begin())->isReg());
                end = start + 1; // 这种情况只能是BL
            }
            auto uses = du.uses;
            for (auto &block : func->getBlocks())
            {
                auto &liveIn = block->getLiveIn();
                auto &liveOut = block->getLiveOut();
                bool in = false;
                bool out = false;
                for (auto &use : uses)
                    if (liveIn.find(use) != liveIn.end())
                    {
                        in = true;
                        break;
                    }
                for (auto &use : uses)
                    if (liveOut.find(use) != liveOut.end())
                    {
                        out = true;
                        break;
                    }
                if (in)
                    start = std::min(start, (*(block->begin()))->getNo());
                if (out)
                    end = std::max(end, (*(block->rbegin()))->getNo());
                else
                {
                    for (auto &use : uses)
                        if (use->getParent()->getParent() == block)
                        {
                            if (!in)
                                assert(end >= use->getParent()->getNo());
                            end = std::max(end, use->getParent()->getNo());
                        }
                }
            }
            if ((*du.defs.begin())->isReg())
                regIntervals.push_back(new Interval({start, end, 0, (*du.defs.begin())->getReg(), (*du.defs.begin())->getValType(), du.defs, du.uses}));
            else
                intervals.push_back(new Interval({start, end, 0, -1, (*du.defs.begin())->getValType(), du.defs, du.uses}));
        }
    }

    sort(regIntervals.begin(), regIntervals.end(), compareStart);
    std::reverse(regIntervals.begin(), regIntervals.end());
    sort(intervals.begin(), intervals.end(), compareStart);
    // // print all intervals
    // fprintf(stderr, "------------------\n");
    // for (auto &interval : regIntervals)
    // {
    //     fprintf(stderr, "%s regInterval: %d %d\n", (*interval->defs.begin())->toStr().c_str(), interval->start, interval->end);
    // }
    // for (auto &interval : intervals)
    // {
    //     fprintf(stderr, "%s VregInterval: %d %d\n", (*interval->defs.begin())->toStr().c_str(), interval->start, interval->end);
    // }
}

bool LinearScan::linearScanRegisterAllocation()
{
    active.clear();
    bool success = true;
    for (auto &interval : intervals)
    {
        expireOldIntervals(interval);

        insertActiveRegIntervals(interval);

        if (interval->valType->isFloat())
        {
            if (!sregs.size())
            {
                spillAtInterval(interval);
                success = false;
            }
            else
            {
                interval->real_reg = (*sregs.rbegin());
                sregs.pop_back();
                auto insertPos = std::lower_bound(active.begin(), active.end(), interval, compareEnd);
                active.insert(insertPos, interval);
            }
        }
        else
        {
            if (!rregs.size())
            {
                spillAtInterval(interval);
                success = false;
            }
            else
            {
                interval->real_reg = (*rregs.rbegin());
                rregs.pop_back();
                auto insertPos = std::lower_bound(active.begin(), active.end(), interval, compareEnd);
                active.insert(insertPos, interval);
            }
        }
    }
    // fprintf(stderr, "------------------\n");
    // fprintf(stderr, "one iteration finished\n");
    return success;
}

void LinearScan::modifyCode()
{
    for (auto &interval : intervals)
    {
        assert(interval->real_reg != -1 && "unallocated vreg detected");
        bool save = true;
        auto func_se = dynamic_cast<IdentifierSymbolEntry *>(func->getSymPtr());
        for (auto kv : func_se->getOccupiedRegs())
        {
            if (kv.first == interval->real_reg && ((kv.second->isFloat() && interval->valType->isFloat()) || (kv.second->isInt() && interval->valType->isInt())))
            {
                save = false;
                break;
            }
        }
        if (interval->real_reg < 4)
        {
            for (auto param : func_se->getParamsSe())
            {
                if (param->getParamNo() == interval->real_reg && ((param->getType()->isFloat() && interval->valType->isFloat()) || ((param->getType()->isInt() || param->getType()->isPTR()) && interval->valType->isInt())))
                {
                    save = false;
                    break;
                }
            }
        }
        auto retType = dynamic_cast<FunctionType *>(func_se->getType())->getRetType();
        if (interval->real_reg == 0 && ((retType->isFloat() && interval->valType->isFloat()) || (retType->isInt() && interval->valType->isInt())))
            save = false;
        if (save)
            func->addSavedRegs(interval->real_reg, interval->valType->isFloat());
        for (auto def : interval->defs)
            def->setReg(interval->real_reg);
        for (auto use : interval->uses)
            use->setReg(interval->real_reg);
    }
}

void LinearScan::genSpillCode()
{
    for (auto &interval : intervals)
    {
        if (interval->real_reg != -1)
            continue;
        /* HINT:
         * The vreg should be spilled to memory.
         * 1. insert ldr inst before the use of vreg
         * 2. insert str inst after the def of vreg
         */
        interval->disp = func->AllocSpace(/*interval->valType->getSize()*/ 4);
        for (auto &use : interval->uses)
        {
            auto block = use->getParent()->getParent();
            auto pos = use->getParent();
            if (!pos->isBL() && !pos->isDummy())
            {
                auto offset = new MachineOperand(MachineOperand::IMM, -interval->disp);
                if (offset->isIllegalShifterOperand())
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

        for (auto &def : interval->defs)
        {
            auto block = def->getParent()->getParent();
            auto pos = def->getParent();
            if (!pos->isBL() && !pos->isDummy())
            {
                auto offset = new MachineOperand(MachineOperand::IMM, -interval->disp);
                if (offset->isIllegalShifterOperand())
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

void LinearScan::expireOldIntervals(Interval *interval)
{
    for (auto inter = active.begin(); inter != active.end(); inter = active.erase(inter))
    {
        if ((*inter)->end > interval->start)
            return;
        if ((*inter)->valType->isFloat())
            sregs.push_back((*inter)->real_reg);
        else
            rregs.push_back((*inter)->real_reg);
    }
}

// take the pre-allocation into account
void LinearScan::insertActiveRegIntervals(Interval *interval)
{
    for (auto inter = regIntervals.rbegin(); inter != regIntervals.rend();)
    {
        if ((*inter)->start >= interval->end)
            return;
        if ((*inter)->end <= interval->start)
        {
            freeIntervals.insert(*inter);
            inter = decltype(inter)(regIntervals.erase(--(inter.base())));
            continue;
        }
        if ((*inter)->valType->isFloat())
        {
            auto it = std::find(sregs.begin(), sregs.end(), (*inter)->real_reg);
            if (it != sregs.end())
            {
                sregs.erase(it);
                auto insertPos = std::lower_bound(active.begin(), active.end(), *inter, compareEnd);
                active.insert(insertPos, *inter);
                freeIntervals.insert(*inter);
                inter = decltype(inter)(regIntervals.erase(--(inter.base())));
            }
            else
                inter++;
        }
        else
        {
            assert((*inter)->valType->isInt());
            auto it = std::find(rregs.begin(), rregs.end(), (*inter)->real_reg);
            if (it != rregs.end())
            {
                rregs.erase(it);
                auto insertPos = std::lower_bound(active.begin(), active.end(), *inter, compareEnd);
                active.insert(insertPos, *inter);
                freeIntervals.insert(*inter);
                inter = decltype(inter)(regIntervals.erase(--(inter.base())));
            }
            else
                inter++;
        }
    }
}

void LinearScan::spillAtInterval(Interval *interval)
{
    // fprintf(stderr, "------------------\n");
    // fprintf(stderr, "overfull: %s [%d,%d]\n", (*interval->defs.begin())->toStr().c_str(), interval->start, interval->end);
    // for (auto &interval : active)
    //     fprintf(stderr, "cur_active: %s [%d,%d]\n", (*interval->defs.begin())->toStr().c_str(), interval->start, interval->end);

    // Interval *toSpill = nullptr;

    auto inter = active.rbegin();
    for (; inter != active.rend() && (*(*inter)->defs.begin())->isReg(); inter++)
        ;
    if (inter != active.rend() && (*inter)->end > interval->end)
    {

        // toSpill = (*inter);

        interval->real_reg = (*inter)->real_reg;
        (*inter)->real_reg = -1;
        decltype(inter)(active.erase(--(inter.base())));
        auto insertPos = std::lower_bound(active.begin(), active.end(), interval, compareEnd);
        active.insert(insertPos, interval);
    }
    else
    {

        // toSpill = interval;

        interval->real_reg = -1;
    }

    // fprintf(stderr, "spill at %s:[%d,%d]\n", (*toSpill->defs.begin())->toStr().c_str(), toSpill->start, toSpill->end);
}
