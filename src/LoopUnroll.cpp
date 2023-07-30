#include "LoopUnroll.h"

static Operand *copyOperand(Operand *ope)
{
    if (ope->getEntry()->getType()->isConst())
        return new Operand(new ConstantSymbolEntry(ope->getType(), ope->getEntry()->getValue()));
    else
        return new Operand(new TemporarySymbolEntry(ope->getType(), SymbolTable::getLabel()));
}

void Loop::PrintInfo() {
    for (auto b : bb)
        fprintf(stderr, "BasicBlock[%d] ", b->getNo());
    
    fprintf(stderr, "\nisInerloop: %d\n", InnerLoop);
    fprintf(stderr, "loopdepth: %d\n", loop_depth);
}

Loop::Loop(std::set<BasicBlock*> origin_bb) {
    bb.clear();
    for (auto b : origin_bb)
        bb.insert(b);
}

void LoopStruct::PrintInfo() {
    fprintf(stderr, "The loopstruct contains basicblock: ");
    origin_loop->PrintInfo();

    fprintf(stderr, "cond bb is %d\n", loopstruct.first->getNo());
    fprintf(stderr, "body bb is %d\n", loopstruct.second->getNo());
}

void LoopAnalyzer::Analyze(Function* f) {
    edgeType.clear();
    loopDepth.clear();
    visit.clear();
    Loops.clear();

    for (auto& bb : f->getBlockList())
        loopDepth[bb] = 0;

    dfs(f->getEntry(), 0);
    computeLoopDepth();
}

int LoopAnalyzer::dfs(BasicBlock* bb, int pre_order) {
    visit.insert(bb);
    preOrder[bb] = pre_order ++;
    fprintf(stderr, "BasicBlock[%d] PreOrder is %d\n", bb->getNo(), preOrder[bb]);
    int post = 0;
    for (auto b = bb->succ_begin(); b != bb->succ_end(); b ++) {
        if (visit.find(*b) == visit.end()) {
            edgeType[{bb, *b}] = TREE;
            post += dfs(*b, pre_order + post);
        }
        else if (preOrder[*b] > preOrder[bb])
            edgeType[{bb, *b}] = FORWARD;
        else if (PostOrder.find(*b) == PostOrder.end()) 
            edgeType[{bb, *b}] = BACKWARD;
        else 
            edgeType[{bb, *b}] = CROSS;
    }
    PostOrder[bb] = post;
    fprintf(stderr, "BasicBlock[%d] PostOrder is %d\n", bb->getNo(), PostOrder[bb]);
    return PostOrder[bb] + 1;
}

std::set<BasicBlock*> LoopAnalyzer::computeNaturalLoop(BasicBlock* cond, BasicBlock* body) {
    std::set<BasicBlock*> loop;
    std::queue<BasicBlock*> q;

    if (cond == body) 
        loop.insert(cond);
    else 
    {
        loop.insert(cond);
        loop.insert(body);
        q.push(cond);
    }
    while (!q.empty()) {
        auto t = q.front();
        q.pop();
        for (auto b = t->succ_begin(); b != t->succ_end(); b ++ )
            if (loop.find(*b) == loop.end())
            {
                q.push(*b);
                loop.insert(*b);
            }
    }
    return loop;
}

void LoopAnalyzer::computeLoopDepth() {
    for (auto edge : edgeType) 
        if (edge.second == BACKWARD)
        {
            auto loop = new Loop(computeNaturalLoop(edge.first.first, edge.first.second));
            auto loopstruct = new LoopStruct(loop);
            loopstruct->SetBody(edge.first.second);
            loopstruct->SetCond(edge.first.first);
            Loops.insert(loopstruct);
            for (auto& b : loop->GetBasicBlock())
                loopDepth[b] ++;
        }
}

bool LoopAnalyzer::isSubset(std::set<BasicBlock*> t_son, std::set<BasicBlock*> t_fat) {
    for (auto s : t_son)
        if (t_fat.find(s) == t_fat.end())
            return false;
    
    return t_son.size() != t_fat.size();
}

void LoopAnalyzer::FindLoops(Function* f) {
    Analyze(f);

    for (auto l : getLoops())
    {
        l->GetLoop()->SetDepth(0x3fffffff);
        for (auto bb : l->GetLoop()->GetBasicBlock())
            l->GetLoop()->SetDepth(std::min(getLoopDepth(bb), l->GetLoop()->GetDepth()));
    }

    for (auto l : getLoops())
        l->GetLoop()->SetInnerLoop();

    for (auto l1 : getLoops())
        for (auto l2 : getLoops())
            if (isSubset(l1->GetLoop()->GetBasicBlock(), l2->GetLoop()->GetBasicBlock()))
                l2->GetLoop()->ClearInnerLoop();
}

void LoopAnalyzer::PrintInfo(Function* f) {
    std::string FunctionName = ((IdentifierSymbolEntry*)f->getSymPtr())->getName();
    fprintf(stderr, "Function %s's Loop Analysis :\n", FunctionName.c_str());
    for (auto l : Loops)
        l->PrintInfo();
    
    fprintf(stderr, "\n");
}

void LoopUnroll::pass() {
    for (auto f : unit->getFuncList()) {
        analyzer.FindLoops(f);
        Loops = analyzer.getLoops();
        auto cand = FindCandidateLoop();
        for (auto candidate : cand)
            Unroll(candidate);
    }
}

std::vector<LoopStruct*> LoopUnroll::FindCandidateLoop() {
    
}

void LoopUnroll::Unroll(LoopStruct *)
{
}
