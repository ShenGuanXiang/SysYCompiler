#ifndef __LOOPANALYZER_H__
#define __LOOPANALYZER_H__

#include "Unit.h"
#include <queue>

struct Loop
{
    std::set<BasicBlock *> loop_bbs;
    int loop_depth;
    bool isInnerLoop;
    bool isOuterLoop;
    BasicBlock *header_bb;
    BasicBlock *exit_bb;

    Loop(std::set<BasicBlock *> loop_bbs = std::set<BasicBlock *>(), int loop_depth = 0,
         bool isInnerLoop = true, bool isOuterLoop = true,
         BasicBlock *header_bb = nullptr, BasicBlock *exit_bb = nullptr)
    {
        this->loop_bbs = loop_bbs;
        this->loop_depth = loop_depth;
        this->isInnerLoop = isInnerLoop;
        this->isOuterLoop = isOuterLoop;
        this->header_bb = header_bb;
        this->exit_bb = exit_bb;
    }
};

class LoopAnalyzer
{
private:
    Function *func;
    std::set<std::pair<BasicBlock *, BasicBlock *>> backEdges;
    std::map<BasicBlock *, int> loopDepthMap;
    std::set<Loop *> loops;

public:
    LoopAnalyzer(Function *func)
    {
        this->func = func;
        backEdges = std::set<std::pair<BasicBlock *, BasicBlock *>>();
        loopDepthMap = std::map<BasicBlock *, int>();
        loops = std::set<Loop *>();
    }

    std::set<BasicBlock *> computeNaturalLoop(BasicBlock *, BasicBlock *);
    void getBackEdges();
    bool isSubset(std::set<BasicBlock *> t_son, std::set<BasicBlock *> t_fat);
    void analyze();
    std::map<BasicBlock *, int> &getLoopDepth() { return loopDepthMap; };
    std::set<Loop *> &getLoops() { return this->loops; };

    // ~LoopAnalyzer()
    // {
    //     for (auto loop : loops)
    //     {
    //         delete loop;
    //     }
    // }
};

struct MLoop
{
    std::set<MachineBlock *> loop_bbs;
    MachineBlock *header_bb;
    MachineBlock *exit_bb;

    MLoop(std::set<MachineBlock *> loop_bbs = std::set<MachineBlock *>(), MachineBlock *header_bb = nullptr, MachineBlock *exit_bb = nullptr)
    {
        this->loop_bbs = loop_bbs;
        this->header_bb = header_bb;
        this->exit_bb = exit_bb;
    };
};

class MLoopAnalyzer
{
private:
    MachineFunction *func;
    std::set<std::pair<MachineBlock *, MachineBlock *>> backEdges;
    std::map<MachineBlock *, int> loopDepthMap;
    std::set<MLoop *> loops;

public:
    MLoopAnalyzer(MachineFunction *func)
    {
        this->func = func;
        backEdges = std::set<std::pair<MachineBlock *, MachineBlock *>>();
        loopDepthMap = std::map<MachineBlock *, int>();
        loops = std::set<MLoop *>();
    }

    std::set<MachineBlock *> computeNaturalLoop(MachineBlock *, MachineBlock *);
    void getBackEdges();
    void analyze();
    std::map<MachineBlock *, int> &getLoopDepth() { return loopDepthMap; };
    std::set<MLoop *> &getLoops() { return this->loops; };

    // ~MLoopAnalyzer()
    // {
    //     for (auto loop : loops)
    //     {
    //         delete loop;
    //     }
    // }
};

#endif