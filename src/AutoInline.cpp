#include "AutoInline.h"
#include <unordered_map>
#include <queue>

void Unit::getCallGraph()
{
    for (auto f : this->getFuncList())
    {
        f->getCallees().clear();
        f->getCallers().clear();
    }
    for (auto f : this->getFuncList())
    {
        for (auto bb : f->getBlockList())
            for (auto instr = bb->begin(); instr != bb->end(); instr = instr->getNext())
            {
                if (instr->isCall())
                {
                    auto func_se = ((FuncCallInstruction *)instr)->GetFuncSe();
                    if (!func_se->isLibFunc())
                    {
                        f->getCallees().insert(func_se->GetFunction());
                        func_se->GetFunction()->getCallers().insert(f);
                    }
                }
            }
    }
}

// 在这里之判断是否是递归函数，如果是递归函数的话就不展开，日后可以在这个位置加一些对于可以内连的判断
bool AutoInliner::ShouldBeinlined(Function *f)
{
    return !f->isRecur();
}

void AutoInliner::pass()
{
    DeadCodeElim dce(unit);
    dce.pass();
    CallIntrNum();
    RecurDetect();
    std::queue<Function *> func_inline;
    for (auto f : unit->getFuncList())
        if (!((IdentifierSymbolEntry *)f->getSymPtr())->isLibFunc())
        {
            bool flag = true;
            for (auto ff : f->getCallees())
                if (!ff->getCallees().empty())
                {
                    flag = false;
                    break;
                }
            if (flag)
                func_inline.push(f);
        }

    while (!func_inline.empty())
    {
        auto f = func_inline.front();
        func_inline.pop();
        for (auto bb : f->getBlockList())
            for (auto instr = bb->begin(); instr != bb->end(); instr = instr->getNext())
            {
                if (instr->isCall() &&
                    ShouldBeinlined(((IdentifierSymbolEntry *)((FuncCallInstruction *)instr)->GetFuncSe())->GetFunction()))
                    pass(instr);
            }
        for (auto o : f->getCallers())
        {
            o->getCallees().erase(o);
            if (o->getCallees().empty() && ShouldBeinlined(o))
                func_inline.push(o);
        }
    }
    dce.deleteUselessFunc();
}

void AutoInliner::pass(Instruction *instr)
{
    auto func_se = ((FuncCallInstruction *)instr)->GetFuncSe();
    auto func = func_se->GetFunction(), Func = instr->getParent()->getParent();
    if (func_se->isLibFunc() || func == nullptr)
        return;
    auto instr_bb = instr->getParent(), exit_bb = new BasicBlock(Func);
    auto Ret = instr->getDef();
    auto params = instr->getUses();
}

void AutoInliner::CallIntrNum()
{
    for (auto it = unit->begin(); it != unit->end(); it++)
    {
        int num = 0;
        auto func = *it;
        for (auto block : func->getBlockList())
            for (auto in = block->begin(); in != block->end();
                 in = in->getNext())
                if (in->isCalc())
                    num++;
        func->SetCalcInstNum(num);
    }
}

void AutoInliner::RecurDetect()
{
    unit->getCallGraph();
    for (auto cur_f : unit->getFuncList())
    {
        for (auto pairs : cur_f->getCallees())
        {
            if (cur_f->isRecur())
                continue;
            std::set<Function *> Path;
            UpdateRecur(cur_f, Path);
        }
    }
}

void AutoInliner::UpdateRecur(Function *f, std::set<Function *> &Path)
{
    for (auto f_nxt : f->getCallees())
        if (Path.count(f_nxt))
        {
            f_nxt->SetRecur();
            return;
        }
        else
        {
            Path.insert(f_nxt);
            UpdateRecur(f_nxt, Path);
            Path.erase(f_nxt);
        }
}