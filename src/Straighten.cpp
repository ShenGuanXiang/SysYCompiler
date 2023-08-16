#include "Straighten.h"
#include <queue>

const int max_iter = 10;
const int max_bb_size = 1000;

void Straighten::pass()
{
    for (auto func : unit->getFuncs())
    {
        bool change = false;
        int cur_iter = 0;
        do
        {
            for (auto dst_bb : func->getBlocks())
            {
                if (dst_bb->getSuccs().size() == 1 && dst_bb->getInsts().size() < max_bb_size && dst_bb->getSuccs()[0]->getInsts().size() < max_bb_size)
                {
                    change = true;

                    auto src_bb = dst_bb->getSuccs()[0];
                    // fprintf(stderr, "copy .L%d to .L%d\n", src_bb->getNo(), dst_bb->getNo());
                    assert((*dst_bb->rbegin())->isBranch() && (*dst_bb->rbegin())->getOpType() == BranchMInstruction::B);
                    // copy insts
                    delete (*dst_bb->rbegin());
                    for (auto minst_iter = src_bb->begin(); minst_iter != src_bb->end(); minst_iter++)
                    {
                        auto copy_minst = (*minst_iter)->deepCopy();
                        dst_bb->insertBack(copy_minst);
                    }
                    // update CFG
                    dst_bb->removeSucc(src_bb);
                    src_bb->removePred(dst_bb);
                    for (auto succ_bb : src_bb->getSuccs())
                        dst_bb->addSucc(succ_bb);
                }
            }

            // delete unreachable
            std::set<MachineBlock *> visited;
            std::queue<MachineBlock *> q;
            q.push(func->getEntry());
            visited.insert(func->getEntry());
            while (!q.empty())
            {
                auto cur_bb = q.front();
                q.pop();
                for (auto succ_bb : cur_bb->getSuccs())
                {
                    if (!visited.count(succ_bb))
                    {
                        visited.insert(succ_bb);
                        q.push(succ_bb);
                    }
                }
            }
            auto blockList = func->getBlocks();
            for (auto bb : blockList)
            {
                if (!visited.count(bb))
                    delete bb;
            }

            cur_iter++;

        } while (change && cur_iter < max_iter);
    }
}