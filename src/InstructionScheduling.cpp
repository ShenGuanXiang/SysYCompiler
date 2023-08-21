#include "InstructionScheduling.h"

#include <stack>
#include <algorithm>

InstructionScheduling::InstructionScheduling(MachineUnit* mUnit) {
    this->mUnit = mUnit;
}

void InstructionScheduling::pass() {
    for (auto func_iter = mUnit->begin(); func_iter != mUnit->end(); func_iter++) 
    {
        auto func = *func_iter;
        for (auto block_iter = func->begin(); block_iter != func->end(); block_iter++) 
        {
            block = *block_iter;
            if (block->getInsts().empty()) 
                continue;
            if (init()) 
            {
                calculate_dep();
                build();
                compute_priority();
                schedule();
                modify();
            }

        }
    }
}

bool InstructionScheduling::init() 
{
    std::vector<MachineInstruction*>::iterator inst_end = block->getInsts().begin();

    for (auto it = block->getInsts().begin(); it+1 != block->getInsts().end(); it++) 
    {
        if ((*(it+1))->isBranch() || (*(it+1))->isCmp() || (*(it+1))->isVmrs()) 
        {
            inst_end = std::find(block->getInsts().begin(), block->getInsts().end(), *it);
            break;
        }   
    }

    insts = std::vector<MachineInstruction*>(block->getInsts().begin(), inst_end + 1);
    if (insts.size() <= 1)
        return 0;

    dep = std::vector<std::vector<int>>(insts.size(),std::vector<int>(insts.size(), 0));

    ready.clear();
    active.clear();

    succs = std::vector<std::vector<int>>(insts.size());
    preds = std::vector<std::vector<int>>(insts.size());

    degree = std::vector<int>(insts.size(), 0);
    priority = std::vector<int>(insts.size(), -1);
    time = std::vector<int>(insts.size(), 0);
    return true;
}



void InstructionScheduling::calculate_dep() {
    for (size_t i = 0; i < insts.size(); i++) 
    {
        int latency = insts[i]->getLatency();
        if (insts[i]->isStack() || insts[i]->isBranch() || insts[i]->isBigReg()) 
        {
            for (size_t j = i + 1; j < insts.size(); j++) 
            {
                dep[i][j] = latency;
            }
        }
        else
        {
            for (size_t j = i + 1; j < insts.size(); j++) 
            {

                for (auto prev_def : insts[i]->getDef())
                {
                    for (auto curr_use : insts[j]->getUse()) {
                        // R-A-W
                        if (*prev_def == *curr_use) {
                            dep[i][j] = latency;
                        }
                    }
                    for (auto curr_def : insts[j]->getDef()) {
                        if (*prev_def == *curr_def) {
                            dep[i][j] = latency;
                        }
                    }
                }

                for (auto prev_use : insts[i]->getUse()) 
                {
                    for (auto curr_def : insts[j]->getDef()) {
                        // W-A-R
                        if (*prev_use == *curr_def) {
                            dep[i][j] = latency;
                        }
                    }
                }
            }

            if (insts[i]->isLoad())
            {
                for (size_t j = i + 1; j < insts.size(); j++) 
                {
                    if (insts[j]->isStore())
                        dep[i][j] = latency;
                }
            }

            if (insts[i]->isStore())
            {
                for (size_t j = i + 1; j < insts.size(); j++) 
                {
                    if (insts[j]->isStore() || insts[j]->isLoad())
                        dep[i][j] = latency;
                }
            }

            if (insts[i]->isCmp())
            {
                for (size_t j = i + 1; j < insts.size(); j++) 
                {
                    if (insts[j]->isCondMov())
                        dep[i][j] = latency;
                }
            }


            if (insts[i]->isBranch())
            {
                for (size_t j = i + 1; j < insts.size(); j++) 
                {
                    if (insts[j]->isBigReg())
                        dep[i][j] = latency;
                }
            }
        }
    }
}





void InstructionScheduling::build() 
{
    for (size_t i = 0; i < insts.size(); i++) 
    {
        for (size_t j = 0; j < insts.size(); j++) 
        {
            if (dep[i][j] != 0) 
            {
                succs[i].push_back(j);
                preds[j].push_back(i);
                degree[j]++;
            }
        }
    }
}


void InstructionScheduling::compute_priority() 
{
    std::stack<int> stack;
    for (size_t i = 0; i < priority.size(); i++) 
        stack.push(i);

    while (!stack.empty()) 
    {
        int x = stack.top();
        stack.pop();

        if (preds[x].empty())
        {
            priority[x] = 0;
        } 
        else 
        {
            int maxPriority = 0;
            for (auto i : preds[x]) 
            {
                if (priority[i] == -1) 
                    stack.push(i);  // Push unprocessed nodes onto the stack
                
                else 
                    maxPriority = std::max(maxPriority, priority[i] + dep[i][x]);
                
            }
            priority[x] = maxPriority;
        }
    }
}

void InstructionScheduling::schedule() 
{
    int cycles = 0;
    new_insts.clear();
    active.clear();
    auto cmpPriority = [&](int x, int y) -> bool { return priority[x] > priority[y]; };

    for (size_t i = 0; i < preds.size(); i++) 
    {
        if (preds[i].empty())
            ready.push_back(i);
    }
    
    while (!ready.empty() || !active.empty()) 
    {
        std::set<int> toInsert;
        std::sort(ready.begin(), ready.end(), cmpPriority);
        for (auto& i : ready) 
        {
            new_insts.push_back(insts[i]);
            time[i] = cycles;
            active.push_back(i);
        }
        ready.clear();



        for (auto i : toInsert) 
        {
            ready.push_back(i);
        }
        cycles++;

        for (auto it = active.begin(); it != active.end();) 
        {
            if (time[*it] + insts[*it]->getLatency() <= cycles) 
            {
                for (auto i : succs[*it]) 
                {
                    degree[i]--;
                    if (degree[i] == 0) 
                    {
                        ready.push_back(i);
                    }
                }
                it = active.erase(it);
            } 
            else 
            {
                it++;
            }
        }

    }
}

void InstructionScheduling::modify() 
{
    std::vector<MachineInstruction*>::iterator nb_end = block->getInsts().end();
    for (auto it = block->getInsts().begin(); it+1 != block->getInsts().end(); it++) 
    {
        if ((*(it+1))->isBranch() || (*(it+1))->isCmp() || (*(it+1))->isVmrs()) 
        {
            nb_end = std::find(block->getInsts().begin(), block->getInsts().end(), *it) + 1;
            break;
        }
    }

    for (; nb_end != block->getInsts().end(); nb_end ++) 
    {
        new_insts.push_back(*nb_end);
    }

    block->getInsts() = new_insts;
}