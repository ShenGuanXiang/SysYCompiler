#include "ElimPHI.h"
#include "SimplifyCFG.h"

static std::set<Instruction *> freeList; // ALL PHIs

void ElimPHI::pass()
{
    auto Funcs = std::vector<Function *>(unit->begin(), unit->end());
    for (auto func : Funcs)
    {
        std::map<BasicBlock *, std::vector<Instruction *>> pcopy;
        auto blocks = std::vector<BasicBlock *>(func->begin(), func->end());
        // Critical Edge Splitting Algorithm for making non-conventional SSA form conventional
        for (auto bb : blocks)
        {
            if (!bb->begin()->isPHI())
                continue;
            auto preds = std::vector<BasicBlock *>(bb->pred_begin(), bb->pred_end());
            for (auto pred : preds)
            {
                // split
                if (pred->getNumOfSucc() > 1) // == 2
                {
                    BasicBlock *splitBlock = new BasicBlock(func);
                    // pred->splitBlock
                    CondBrInstruction *branch = (CondBrInstruction *)(pred->rbegin());
                    if (branch->getTrueBranch() == bb)
                        branch->setTrueBranch(splitBlock);
                    else
                        branch->setFalseBranch(splitBlock);
                    pred->addSucc(splitBlock);
                    pred->removeSucc(bb);
                    splitBlock->addPred(pred);
                    // splitBlock->bb
                    new UncondBrInstruction(bb, splitBlock);
                    splitBlock->addSucc(bb);
                    bb->addPred(splitBlock);
                    bb->removePred(pred);
                    for (auto i = bb->begin(); i != bb->end() && i->isPHI(); i = i->getNext())
                    {
                        auto def = i->getDef();
                        auto src = dynamic_cast<PhiInstruction *>(i)->getSrcs()[pred];
                        src->removeUse(i);
                        pcopy[splitBlock].push_back(new BinaryInstruction(BinaryInstruction::ADD, def, src, new Operand(new ConstantSymbolEntry(Var2Const(def->getType()), 0))));
                        freeList.insert(i);
                    }
                }
                // no split
                else
                {
                    for (auto i = bb->begin(); i != bb->end() && i->isPHI(); i = i->getNext())
                    {
                        auto def = i->getDef();
                        auto src = dynamic_cast<PhiInstruction *>(i)->getSrcs()[pred];
                        src->removeUse(i);
                        pcopy[pred].push_back(new BinaryInstruction(BinaryInstruction::ADD, def, src, new Operand(new ConstantSymbolEntry(Var2Const(def->getType()), 0))));
                        freeList.insert(i);
                    }
                }
            }
            for (auto i : freeList)
                i->getParent()->remove(i);
        }
        // Replacement of parallel copies with sequences of sequential copy operations.
        for (auto kv : pcopy)
        {
            auto block = kv.first;
            auto &restInsts = kv.second;
            std::vector<Instruction *> seq;
            auto insts = restInsts;
            for (auto inst : insts) // delete inst like a <- a
            {
                if (inst->getDef() == inst->getUses()[0])
                    restInsts.erase(std::find(restInsts.begin(), restInsts.end(), inst));
            }
            while (restInsts.size())
            {
                std::set<Operand *> uses;
                for (auto inst : restInsts)
                    uses.insert(inst->getUses()[0]);
                bool found = false;
                auto Insts = restInsts;
                for (auto inst : Insts)
                {
                    if (uses.count(inst->getDef()) == 0) // inst is not live-in of pcopy
                    {
                        seq.push_back(inst);
                        restInsts.erase(std::find(restInsts.begin(), restInsts.end(), inst));
                        found = true;
                    }
                }
                // pcopy is only made-up of cycles; Break one of them
                if (!found)
                {
                    auto freshOp = new Operand(new TemporarySymbolEntry(restInsts[0]->getUses()[0]->getType(), SymbolTable::getLabel()));
                    seq.push_back(new BinaryInstruction(BinaryInstruction::ADD, freshOp, restInsts[0]->getUses()[0], new Operand(new ConstantSymbolEntry(Var2Const(freshOp->getType()), 0))));
                    restInsts[0]->getUses()[0] = freshOp;
                }
            }
            for (auto inst : seq)
                block->insertBefore(inst, block->rbegin()); // 跳过branch指令
        }
    }
    for (auto i : freeList){
        delete i;
    }
    freeList.clear();
    SimplifyCFG sc(unit);
    sc.pass();
}