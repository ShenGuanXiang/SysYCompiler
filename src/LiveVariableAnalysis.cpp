#include "LiveVariableAnalysis.h"
#include "Unit.h"
#include "MachineCode.h"
#include <algorithm>
#include <stack>
#include <map>

// void LiveVariableAnalysis::pass(Unit *unit)
// {
//     for (auto func = unit->begin(); func != unit->end(); func++)
//     {
//         computeDefUse(*func);
//         computeLiveInOut(*func);
//     }
// }

// void LiveVariableAnalysis::pass(Function *func)
// {
//     computeDefUse(func);
//     computeLiveInOut(func);
// }

// void LiveVariableAnalysis::computeDefUse(Function *func)
// {
//     for (auto block = func->begin(); block != func->end(); block++)
//         for (auto inst = (*block)->begin(); inst != (*block)->end(); inst = inst->getNext())
//         {
//             auto uses = inst->getUses();
//             set_difference(uses.begin(), uses.end(), def[(*block)].begin(), def[(*block)].end(),
//                            inserter(use[(*block)], use[(*block)].end()));
//             auto defs = inst->getDef();
//             for (auto &d : defs)
//                 def[(*block)].insert(all_uses[*d].begin(), all_uses[*d].end());
//         }
// }

// void LiveVariableAnalysis::computeLiveInOut(Function *func)
// {
//     for (auto block = func->begin(); block != func->end(); block++)
//         (*block)->getLiveIn().clear();
//     bool change = true;
//     while (change)
//     {
//         change = false;
//         for (auto block = func->begin(); block != func->end(); block++)
//         {
//             (*block)->getLiveOut().clear();
//             auto old = (*block)->getLiveIn();
//             for (auto succ = (*block)->succ_begin(); succ != (*block)->succ_end(); succ++)
//                 (*block)->getLiveOut().insert((*succ)->getLiveIn().begin(), (*succ)->getLiveIn().end());
//             (*block)->getLiveIn() = use[(*block)];
//             set_difference((*block)->getLiveOut().begin(), (*block)->getLiveOut().end(),
//                            def[(*block)].begin(), def[(*block)].end(), inserter((*block)->getLiveIn(), (*block)->getLiveIn().end()));
//             if (old != (*block)->getLiveIn())
//                 change = true;
//         }
//     }
// }

void MLiveVariableAnalysis::pass()
{
    for (auto &func : unit->getFuncs())
        func->AnalyzeLiveVariable();
}

void MachineFunction::AnalyzeLiveVariable()
{
    std::map<MachineOperand, std::set<MachineOperand *>> all_uses;
    std::map<MachineBlock *, std::set<MachineOperand *>> Def, LiveUse;
    for (auto &block : getBlocks())
    {
        for (auto &inst : block->getInsts())
        {
            auto uses = inst->getUse();
            for (auto &use : uses)
            {
                if (!use->isLabel())
                    all_uses[*use].insert(use);
            }
        }
    }
    for (auto &block : getBlocks())
    {
        for (auto inst = block->getInsts().begin(); inst != block->getInsts().end(); inst++)
        {
            std::set<MachineOperand *> uses((*inst)->getUse().begin(), (*inst)->getUse().end());
            set_difference(uses.begin(), uses.end(), Def[block].begin(), Def[block].end(), inserter(LiveUse[block], LiveUse[block].end()));
            auto defs = (*inst)->getDef();
            for (auto &d : defs)
                Def[block].insert(all_uses[*d].begin(), all_uses[*d].end());
        }
    }

    for (auto &block : getBlocks())
        block->getLiveIn().clear();

    // 后序dfs，CFG有环则无可避免多次迭代，这里的预处理作用是加速
    std::vector<MachineBlock *> dfnList;
    std::map<MachineBlock *, bool> is_visited, in_stk;
    for (auto &block : getBlocks())
    {
        is_visited[block] = false;
        in_stk[block] = false;
    }
    std::stack<MachineBlock *> st;
    st.push(getEntry());
    in_stk[getEntry()] = true;
    while (!st.empty())
    {
        auto mBB = st.top();
        bool all_visited = true;
        for (auto succ : mBB->getSuccs())
        {
            if (!is_visited[succ] && !in_stk[succ])
            {
                st.push(succ);
                in_stk[succ] = true;
                all_visited = false;
                break;
            }
        }
        if (all_visited)
        {
            st.pop();
            is_visited[mBB] = true;
            in_stk[mBB] = false;
            dfnList.push_back(mBB);
        }
    }

    bool change = true;
    while (change)
    {
        change = false;
        for (auto block : dfnList)
        {
            block->getLiveOut().clear();
            auto old = block->getLiveIn();
            for (auto &succ : block->getSuccs())
                block->getLiveOut().insert(succ->getLiveIn().begin(), succ->getLiveIn().end());
            block->getLiveIn() = LiveUse[block];
            set_difference(block->getLiveOut().begin(), block->getLiveOut().end(), Def[block].begin(), Def[block].end(), inserter(block->getLiveIn(), block->getLiveIn().end()));
            if (old != block->getLiveIn())
                change = true;
        }
    }
}