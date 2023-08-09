#ifndef __LOOPUNROLL_H__
#define __LOOPUNROLL_H__

#include "Unit.h"
#include <queue>

class Loop {
    // whole loop
private:
    std::set<BasicBlock* > bb;
    int loop_depth = 0;
    bool InnerLoop;
public:
    Loop() { bb.clear(); };
    Loop(std::set<BasicBlock*>);
    std::set<BasicBlock*>& GetBasicBlock() { return bb; };
    void SetDepth(int d) { loop_depth = d; };
    int GetDepth() { return loop_depth; };
    bool isInnerLoop() { return InnerLoop; };
    void SetInnerLoop() { InnerLoop = true; };
    void ClearInnerLoop() { InnerLoop = false; };
    void PrintInfo();
};

class LoopStruct {
    // This class contains the condition bb and main loop body bb for loop
private:
    Loop* origin_loop;
    std::pair<BasicBlock*, BasicBlock*> loopstruct;
public:
    LoopStruct() { loopstruct.first = loopstruct.second = nullptr; };
    LoopStruct(Loop* loop) { origin_loop = loop; };
    BasicBlock* GetCond() { return loopstruct.first; };
    BasicBlock* GetBody() { return loopstruct.second; };
    void SetCond(BasicBlock* cond) { loopstruct.first = cond; };
    void SetBody(BasicBlock* body) { loopstruct.second = body; };
    Loop* GetLoop() { return origin_loop; };
    void PrintInfo();
};

class LoopAnalyzer {
    // initialize the loop info
private:
    std::set<std::pair<BasicBlock*, BasicBlock*>> edgeType;
    // std::map<BasicBlock*, int> preOrder;
    // std::map<BasicBlock*, int> PostOrder;
    std::map<BasicBlock*, int> loopDepth;
    std::set<BasicBlock*> visit;
    
    void computeLoopDepth();
    std::set<BasicBlock*> computeNaturalLoop(BasicBlock*, BasicBlock*);
    std::set<LoopStruct*> Loops;

    bool isSubset(std::set<BasicBlock*> t_son, std::set<BasicBlock*> t_fat);
public:
    void Analyze(Function *);
    void FindLoops(Function *);
    void Get_REdge(BasicBlock* bb);
    int getLoopDepth(BasicBlock* bb) { return loopDepth[bb]; };
    std::set<LoopStruct*>& getLoops() { return this->Loops; };
    void PrintInfo(Function* f);
};

class MLoop {
    // whole loop
private:
    std::set<MachineBlock* > bb;
    int loop_depth = 0;
    bool InnerLoop;
public:
    MLoop() { bb.clear(); };
    MLoop(std::set<MachineBlock*>);
    std::set<MachineBlock*>& GetBasicBlock() { return bb; };
    void SetDepth(int d) { loop_depth = d; };
    int GetDepth() { return loop_depth; };
    bool isInnerLoop() { return InnerLoop; };
    void SetInnerLoop() { InnerLoop = true; };
    void ClearInnerLoop() { InnerLoop = false; };
    void PrintInfo();
};

class MLoopStruct {
    // This class contains the condition bb and main loop body bb for loop
private:
    MLoop* origin_loop;
    std::pair<MachineBlock*, MachineBlock*> loopstruct;
public:
    MLoopStruct() { loopstruct.first = loopstruct.second = nullptr; };
    MLoopStruct(MLoop* loop) { origin_loop = loop; };
    MachineBlock* GetCond() { return loopstruct.first; };
    MachineBlock* GetBody() { return loopstruct.second; };
    void SetCond(MachineBlock* cond) { loopstruct.first = cond; };
    void SetBody(MachineBlock* body) { loopstruct.second = body; };
    MLoop* GetLoop() { return origin_loop; };
    void PrintInfo();
};

/*
Usage:
    MLoopAnalyzer mla;
    // mla.Analyze(machine_func);
    mla.FindLoops(machine_func);
    for(auto bb_it = func->begin();bb_it!=func->end();bb_it++){
        MachineBlock* bb = *bb_it;
        loop_depth[bb] = 0;
    }
    for(auto loop:la.getLoops()){
        for(auto bb:loop->GetLoop()->GetBasicBlock()){
            loop_depth[bb] = std::max(loop->GetLoop()->GetDepth(),loop_depth[bb]);
        }
    }
*/
class MLoopAnalyzer {
    // initialize the loop info
private:
    std::set<std::pair<MachineBlock*, MachineBlock*>> edgeType;
    // std::map<BasicBlock*, int> preOrder;
    // std::map<BasicBlock*, int> PostOrder;
    std::map<MachineBlock*, int> loopDepth;
    std::set<MachineBlock*> visit;
    
    void computeLoopDepth();
    std::set<MachineBlock*> computeNaturalLoop(MachineBlock*, MachineBlock*);
    std::set<MLoopStruct*> Loops;

    bool isSubset(std::set<MachineBlock*> t_son, std::set<MachineBlock*> t_fat);
public:
    void Analyze(MachineFunction *);
    void FindLoops(MachineFunction *);
    void Get_REdge(MachineBlock* bb);
    int getLoopDepth(MachineBlock* bb) { return loopDepth[bb]; };
    std::set<MLoopStruct*>& getLoops() { return this->Loops; };
    void PrintInfo(MachineFunction* f);
};

class LoopUnroll {
private:
    Unit* unit;
    const int MAX_UNROLLING_FACTOR = 4;
    const int MIN_UNROLLING_FACTOR = 2;
    int unrolling_factor;
    LoopAnalyzer analyzer;
    std::set<LoopStruct*> Loops;
public:
    LoopUnroll(Unit* u) : unit(u) {};
    void pass();
    std::vector<LoopStruct*> FindCandidateLoop();
    void Unroll(LoopStruct*);
};


#endif