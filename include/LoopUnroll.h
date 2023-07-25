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
    void PrintInfo();
};

class LoopAnalyzer {
    // initialize the loop info
private:
    enum{NONE, BACKWARD, FORWARD, TREE, CROSS};
    std::map<std::pair<BasicBlock*, BasicBlock*>, unsigned> edgeType;
    std::map<BasicBlock*, int> preOrder;
    std::map<BasicBlock*, int> PostOrder;
    std::map<BasicBlock*, int> loopDepth;
    std::set<BasicBlock*> visit;
    
    void computeLoopDepth();
    std::set<BasicBlock*> computeNaturalLoop(BasicBlock*, BasicBlock*);
    std::set<Loop*> Loops;

    bool isSubset(std::set<BasicBlock*> t_son, std::set<BasicBlock*> t_fat);
public:
    void Analyze(Function *);
    void FindLoops(Function *);
    int dfs(BasicBlock* bb, int pre_order);
    int getLoopDepth(BasicBlock* bb) { return loopDepth[bb]; };
    std::set<Loop*>& getLoops() { return this->Loops; };
    void PrintInfo(Function* f);
};


#endif