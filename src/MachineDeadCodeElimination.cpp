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
    for (auto f : unit->getFuncs())
        pass(f);
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
                bool flag = true;
                for (auto d : defs)
                    if (!out[*d].empty())
                        flag = false;
                if (flag)
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