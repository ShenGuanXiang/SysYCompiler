#include "LoopUnroll.h"

static Operand *copyOperand(Operand *ope)
{
    if (ope->getEntry()->getType()->isConst())
        return new Operand(new ConstantSymbolEntry(ope->getType(), ope->getEntry()->getValue()));
    else
    {
        assert(ope->getEntry()->isTemporary());
        return new Operand(new TemporarySymbolEntry(ope->getType(), SymbolTable::getLabel()));
    }
}

void LoopUnroll::pass()
{
    auto cand = FindCandidateLoop();
    fprintf(stderr, "loop be unrolled\n");
    for (auto candidate : cand)
        Unroll(candidate);
    fprintf(stderr, "loop info finish\n");
}

/*
    Find Candidate Loop, don't consider instrs that have FuncCall temporarily;
*/
std::vector<Loop *> LoopUnroll::FindCandidateLoop()
{
    std::vector<Loop *> Worklist;
    for (auto f : unit->getFuncList())
    {
        auto analyzer = LoopAnalyzer(f);
        analyzer.analyze();
        for (auto loop : analyzer.getLoops())
        {
            Worklist.push_back(loop);
        }
    }
    return Worklist;
}

BasicBlock *LoopUnroll::LastBasicBlock(Operand *op, BasicBlock *bb)
{
    auto res_instr = op->getDef();
    std::map<Instruction *, bool> visited;
    std::queue<Operand *> q_operand;
    visited[res_instr] = true;
    q_operand.push(op);
    while (!q_operand.empty())
    {
    }
}

void LoopUnroll::InitLoopOp(Operand *begin, Operand *stride, Operand *end, Operand *op1, Operand *op2, BasicBlock *bb)
{
    auto Dom = bb->getSDoms();
    if (op1->getType()->isConst())
        end = op1, stride = op2;
    else if (op2->getType()->isConst())
        end = op2, stride = op1;
    else
    {
        // if both ops are not constant, just find the variable change in loop, if both, we do nothing
        auto f1 = op1->getDef()->getParent(), f2 = op2->getDef()->getParent();
        if (Dom.find(f1) != Dom.end() || Dom.find(f2) != Dom.end())
        {
            if (Dom.find(f1) != Dom.end() && Dom.find(f2) != Dom.end())
            {
                fprintf(stderr, "Todo: two loop variable\n");
                return;
            }
            if (Dom.find(f1) != Dom.end())
                stride = op1, end = op2;
            else
                stride = op2, end = op1;
        }
        else
            assert(0);
    }
    if (end == nullptr || stride == nullptr)
        return;
    /*
        big problem, how to get beginop(phiInstruction)
    */
    Instruction *temp = stride->getDef();
    fprintf(stderr, "stride def instr is %s]\n", temp->getDef()->toStr().c_str());
    assert(temp && temp->isPHI());
    // choose src_op outside loop
    for (auto src : ((PhiInstruction *)temp)->getSrcs())
        if (Dom.find(src.first) == Dom.end())
            begin = src.second;
}

void LoopUnroll::Unroll(Loop *loopstruct)
{
    int begin = -1, end = -1, stride = -1;
    bool IsBeginCons, IsEndCons, IsStrideCons;
    IsBeginCons = IsEndCons = IsStrideCons = false;
    BasicBlock *cond = loopstruct->exit_bb, *body = loopstruct->header_bb;
    CmpInstruction *cmp;
    Operand *endOp = nullptr, *beginOp = nullptr, *strideOp = nullptr;

    bool ifcmpInsMatch = true;
    CmpInstruction *condCmp = nullptr;
    CmpInstruction *bodyCmp = nullptr;
    for (auto condinstr = loopstruct->exit_bb->begin(); condinstr != loopstruct->exit_bb->end(); condinstr = condinstr->getNext())
    {
        if (condinstr->isCmp())
        {
            condCmp = (CmpInstruction *)condinstr;
            int opcode = condCmp->getOpcode();
            switch (opcode)
            {
            case CmpInstruction::G:
                break;
            case CmpInstruction::GE:
                break;
            case CmpInstruction::L:
                break;
            case CmpInstruction::LE:
                break;
            default:
                ifcmpInsMatch = false;
                break;
            }
        }
    }

    if (!ifcmpInsMatch)
    {
        fprintf(stderr, "can't do this type cmpInstr\n");
        return;
    }
    for (auto bodyinstr = loopstruct->header_bb->begin(); bodyinstr != loopstruct->header_bb->end();
         bodyinstr = bodyinstr->getNext())
    {
        if (bodyinstr->isCmp())
        {
            bodyinstr->getParent()->getParent()->ComputeDom();
            bodyCmp = (CmpInstruction *)bodyinstr;
            InitLoopOp(beginOp, strideOp, endOp,
                       bodyCmp->getUses()[0], bodyCmp->getUses()[1], bodyCmp->getParent());
            fprintf(stderr, "[LoopUnroll]: begin{%s}, stride{%s}, end{%s}", beginOp->toStr().c_str(),
                    strideOp->toStr().c_str(), endOp->toStr().c_str());
            break;
        }
    }

    if (beginOp == nullptr)
        return;

    /*
        just unroll, fully unroll or vectorization
        if all const
            fully unroll
        else
            unroll part
    */
}
