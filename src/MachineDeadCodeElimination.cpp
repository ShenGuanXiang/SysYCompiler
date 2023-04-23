#include "MachineDeadCodeElimination.h"
#include "LiveVariableAnalysis.h"
#include <vector>
#include <map>
#include <set>

bool MachineInstruction::isBranch() const
{
    return type == BRANCH;
}

bool MachineInstruction::isCondMov() const
{
    return type == MOV && op == 0 && cond != NONE;
}

bool MachineInstruction::isSmull() const
{
    return type == SMULL;
}

bool MachineInstruction::isCritical() const
{
    if (isBL() || isDummy() || isBranch() || isStack())
        return true;
    for (auto def : def_list)
    {
        if (def->getReg() == 13 && def->isReg())
            return true;
    }
    return false;
}

MachineInstruction* MachineBlock::getNext(MachineInstruction* instr)
{
    auto it = find(inst_list.begin(), inst_list.end(), instr);
    if (it != inst_list.end() && (it + 1) != inst_list.end()) {
        return *(it + 1);
    }
    return nullptr;
}

void MachineBlock::remove(MachineInstruction* instr)
{
    auto it = find(inst_list.begin(), inst_list.end(), instr);
    if (it != inst_list.end())
        inst_list.erase(it);
}

void MachineDeadCodeElimination::pass()
{
    for (auto f : unit->getFuncs()) {
        pass(f);
        // SingleBrDelete(f);
    }
}

void MachineDeadCodeElimination::pass(MachineFunction *f)
{
    MLiveVariableAnalysis mlva(nullptr);
    mlva.pass(f);
    std::map<MachineOperand, std::set<MachineOperand *>> out;
    std::vector<MachineInstruction*> deleteList;
    for (auto b : f->getBlocks())
    {
        out.clear();

        for (auto liveout : b->getLiveOut())
            out[*liveout].insert(liveout);

        auto Insts = b->getInsts();
        for (auto itr = Insts.rbegin(); itr != Insts.rend(); itr++)
        {
            auto defs = (*itr)->getDef();
            if (!defs.empty())
            {
                MachineOperand* def = nullptr;
                if((*itr)->isSmull()){
                    def = defs[1];
                }
                else
                    def = defs[0];
                if (out[*def].empty())
                    deleteList.push_back(*itr);
            }

            for (auto& def : defs)
                for (auto& o : mlva.getAllUses()[*def])
                    if (out[*def].find(o) != out[*def].end())
                        out[*def].erase(o);
            auto uses = (*itr)->getUse();
            for (auto& use : uses)
                out[*use].insert(use);
        }
    }
    for (auto t : deleteList) {
        if (t->isCritical())
            continue;
        if (t->isCondMov()) {
            auto next = t->getParent()->getNext(t);
            if (next && next->isCondMov())
                continue;
        }
        if (t)
            t->getParent()->remove(t);
    }
}

void MachineDeadCodeElimination::SingleBrDelete(MachineFunction* f)
{
    std::vector<MachineBlock*> delete_bbs;
    for (auto &bb : f->getBlocks())
    {
        if (bb->getInsts().size() != 1)
            continue;
        MachineInstruction *inst = *bb->getInsts().begin();
        if (!inst->isBranch())
            continue;
        BranchMInstruction *br = dynamic_cast<BranchMInstruction *>(inst);
        delete_bbs.push_back(bb);
        for (auto &pred : bb->getPreds())
        {
            pred->addSucc(bb->getSuccs()[0]);
            bb->getSuccs()[0]->addPred(pred);
            if (pred->getInsts().empty())
                continue;
            MachineInstruction *inst1 = pred->getInsts().back();
            if (!inst1->isBranch())
                continue;
            BranchMInstruction *br1 = dynamic_cast<BranchMInstruction *>(inst1);
            if (br1->getParent()->getNo() == bb->getNo())
            {
                br1->setParent(br->getParent());
            }
            if (pred->getInsts().size() <= 1)
                continue;
            auto it = pred->getInsts().rbegin();
            it++;
            inst1 = *it;
            if (!inst1->isBranch())
                continue;
            br1 = dynamic_cast<BranchMInstruction *>(inst1);
            if (br1->getParent()->getNo() == bb->getNo())
            {
                br1->setParent(br->getParent());
            }
        }
    }

    for (auto bb : delete_bbs)
    {
        for(auto &pred:bb->getPreds())
            pred->getSuccs().erase(std::find(pred->getSuccs().begin(), pred->getSuccs().end(), bb));
        for(auto &succ:bb->getSuccs())
            succ->getPreds().erase(std::find(succ->getPreds().begin(), succ->getPreds().end(), bb));
        f->removeBlock(bb);
    }
}