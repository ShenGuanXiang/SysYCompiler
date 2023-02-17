#include "LiveVariableAnalysis.h"
#include "Unit.h"
#include "MachineCode.h"
#include <algorithm>

void LiveVariableAnalysis::pass(Unit *unit)
{
    for (auto func = unit->begin(); func != unit->end(); func++)
    {
        computeUsePos(*func);
        computeDefUse(*func);
        computeLiveInOut(*func);
    }
}

void LiveVariableAnalysis::pass(Function *func)
{
    computeUsePos(func);
    computeDefUse(func);
    computeLiveInOut(func);
}

void LiveVariableAnalysis::computeUsePos(Function *func)
{
    for (auto block = func->begin(); block != func->end(); block++)
        for (auto inst = (*block)->begin(); inst != (*block)->end(); inst = inst->getNext())
        {
            auto uses = inst->getUses();
            for (auto &use : uses)
                all_uses[*use].insert(use);
        }
}

void LiveVariableAnalysis::computeDefUse(Function *func)
{
    for (auto block = func->begin(); block != func->end(); block++)
        for (auto inst = (*block)->begin(); inst != (*block)->end(); inst = inst->getNext())
        {
            auto uses = inst->getUses();
            set_difference(uses.begin(), uses.end(), def[(*block)].begin(), def[(*block)].end(),
                           inserter(use[(*block)], use[(*block)].end()));
            auto defs = inst->getDef();
            for (auto &d : defs)
                def[(*block)].insert(all_uses[*d].begin(), all_uses[*d].end());
        }
}

void LiveVariableAnalysis::computeLiveInOut(Function *func)
{
    for (auto block = func->begin(); block != func->end(); block++)
        (*block)->getLiveIn().clear();
    bool change;
    change = true;
    while (change)
    {
        change = false;
        for (auto block = func->begin(); block != func->end(); block++)
        {
            (*block)->getLiveOut().clear();
            auto old = (*block)->getLiveIn();
            for (auto succ = (*block)->succ_begin(); succ != (*block)->succ_end(); succ++)
                (*block)->getLiveOut().insert((*succ)->getLiveIn().begin(), (*succ)->getLiveIn().end());
            (*block)->getLiveIn() = use[(*block)];
            set_difference((*block)->getLiveOut().begin(), (*block)->getLiveOut().end(),
                           def[(*block)].begin(), def[(*block)].end(), inserter((*block)->getLiveIn(), (*block)->getLiveIn().end()));
            if (old != (*block)->getLiveIn())
                change = true;
        }
    }
}

void MLiveVariableAnalysis::pass(MachineUnit *unit)
{
    for (auto &func : unit->getFuncs())
    {
        computeUsePos(func);
        computeDefUse(func);
        computeLiveInOut(func);
    }
}

void MLiveVariableAnalysis::pass(MachineFunction *func)
{
    computeUsePos(func);
    computeDefUse(func);
    computeLiveInOut(func);
}

void MLiveVariableAnalysis::computeUsePos(MachineFunction *func)
{
    for (auto &block : func->getBlocks())
    {
        for (auto &inst : block->getInsts())
        {
            auto uses = inst->getUse();
            for (auto &use : uses)
            {
                // assert(!use->isLabel());
                all_uses[*use].insert(use);
            }
        }
    }
}

void MLiveVariableAnalysis::computeDefUse(MachineFunction *func)
{
    for (auto &block : func->getBlocks())
    {
        for (auto inst = block->getInsts().begin(); inst != block->getInsts().end(); inst++)
        {
            auto user = (*inst)->getUse();
            std::set<MachineOperand *> temp(user.begin(), user.end());
            set_difference(temp.begin(), temp.end(),
                           def[block].begin(), def[block].end(), inserter(use[block], use[block].end()));
            auto defs = (*inst)->getDef();
            for (auto &d : defs)
                def[block].insert(all_uses[*d].begin(), all_uses[*d].end());
        }
    }
}

void MLiveVariableAnalysis::computeLiveInOut(MachineFunction *func)
{
    for (auto &block : func->getBlocks())
        block->getLiveIn().clear();
    bool change;
    change = true;
    while (change)
    {
        change = false;
        for (auto &block : func->getBlocks())
        {
            block->getLiveOut().clear();
            auto old = block->getLiveIn();
            for (auto &succ : block->getSuccs())
                block->getLiveOut().insert(succ->getLiveIn().begin(), succ->getLiveIn().end());
            block->getLiveIn() = use[block];
            set_difference(block->getLiveOut().begin(), block->getLiveOut().end(),
                           def[block].begin(), def[block].end(), inserter(block->getLiveIn(), block->getLiveIn().end()));
            if (old != block->getLiveIn())
                change = true;
        }
    }
}
