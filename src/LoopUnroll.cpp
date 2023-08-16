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
    for (auto f : unit->getFuncList())
    {
        LoopAnalyzer la(f);
        la.analyze();
        Loops = la.getLoops();
        auto cand = FindCandidateLoop();
        for (auto candidate : cand)
            Unroll(candidate);
    }
}

std::vector<Loop *> LoopUnroll::FindCandidateLoop()
{
    // std::vector<LoopStruct*> Worklist;
    // for (auto f : unit->getFuncList()) {
    //     for(auto loop : analyzer.FindLoops(f)){

    //         // find cond and body
    //         BasicBlock* cond = nullptr, *body = nullptr;
    //         for(auto bb : loop->getbbs()){
    //             for(auto instr = bb->begin(); instr != bb->end()->getNext(); instr = instr->getNext()){
    //                 if(instr->isCmp()){
    //                     cond = bb;
    //                     break;
    //                 }
    //             }
    //             if(cond) break;
    //         }

    //         for(auto bb : loop->getbbs()){
    //             if(bb != cond){
    //                 body = bb;
    //             }
    //         }

    //         LoopStruct* CandidateLoop = new LoopStruct(loop);
    //         CandidateLoop->SetCond(cond);
    //         CandidateLoop->SetBody(body);
    //         Worklist.push_back(CandidateLoop);
    //     }
    // }

    // for(auto CandidateLoop : Worklist){
    //     bool HasCallInBody = false;
    //     for(auto bodyinstr = CandidateLoop->getBody()->begin(); bodyinstr != CandidateLoop->getBody()->end()->getNext(); bodyinstr = bodyinstr->getNext()){
    //         if(bodyinstr->isCall()){
    //             HasCallInBody = true;
    //             break;
    //         }
    //     }
    //     if(HasCallInBody){
    //         Exception("Candidate loop shall have no call in body");
    //         continue;
    //     }

    //     bool CheckFlag = false;
    //     for(auto bb = CandidateLoop->getCond()->succ_begin(); bb != CandidateLoop->getCond()->succ_end(); bb++){
    //         CheckFlag = CheckFlag || (*bb == CandidateLoop->getBody());
    //     }
    //     if(!CheckFlag){
    //         continue;
    //     }
    // }
}

void LoopUnroll::Unroll(Loop *)
{
}
