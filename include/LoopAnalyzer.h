#ifndef __LOOPANALYZER_H__
#define __LOOPANALYZER_H__

#include "Unit.h"
#include <queue>

struct InductionVar
{
    std::vector<std::vector<Instruction *>> du_chains;
    InductionVar(std::vector<std::vector<Instruction *>> du_chains)
    {
        this->du_chains = du_chains;
    }
    void printInductionVar()
    {
        fprintf(stderr, "induction_var:\n");
        for (auto du_chain : du_chains)
        {
            for (auto inst : du_chain)
                fprintf(stderr, "%s->", inst->getDef()->toStr().c_str());
            fprintf(stderr, "%s\n", du_chain[0]->getDef()->toStr().c_str());
        }
    }
};

struct Loop
{
    std::set<BasicBlock *> loop_bbs;
    BasicBlock *header_bb;
    BasicBlock *exiting_bb;
    std::set<Loop *> subLoops;
    Loop *parentLoop;
    int loop_depth;
    std::set<InductionVar *> inductionVars;

    Loop(std::set<BasicBlock *> loop_bbs = std::set<BasicBlock *>(),
         BasicBlock *header_bb = nullptr, BasicBlock *exiting_bb = nullptr)
    {
        this->loop_bbs = loop_bbs;
        this->header_bb = header_bb;
        this->exiting_bb = exiting_bb;
        this->subLoops = std::set<Loop *>();
        this->parentLoop = nullptr;
        this->loop_depth = 0;
        this->inductionVars = std::set<InductionVar *>();
    }

    void printLoop()
    {
        fprintf(stderr, "Loop Info:\nloop_bbs:{");
        for (auto bb : loop_bbs)
        {
            fprintf(stderr, "BB%d, ", bb->getNo());
        }
        fprintf(stderr, "}\nheader_bb:BB%d, exiting_bb:BB%d, loop_depth:%d\n", header_bb->getNo(), exiting_bb->getNo(), loop_depth);
        for (auto ind_var : inductionVars)
            ind_var->printInductionVar();
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
    std::set<BasicBlock *> getIntersection(std::set<BasicBlock *> loop1, std::set<BasicBlock *> loop2);
    void computeInductionVars(Loop *loop);
    bool multiOr(Loop *loop);
    bool hasLeak(Loop *loop);
    int getLoopCnt(Loop *loop);
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
    MachineBlock *exiting_bb;
    int loop_depth;

    MLoop(std::set<MachineBlock *> loop_bbs = std::set<MachineBlock *>(), MachineBlock *header_bb = nullptr, MachineBlock *exiting_bb = nullptr)
    {
        this->loop_bbs = loop_bbs;
        this->header_bb = header_bb;
        this->exiting_bb = exiting_bb;
        this->loop_depth = 0;
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
    bool isSubset(std::set<MachineBlock *> t_son, std::set<MachineBlock *> t_fat);
    std::set<MachineBlock *> getIntersection(std::set<MachineBlock *> loop1, std::set<MachineBlock *> loop2);
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