#ifndef __LAZYCODEMOTION_H__
#define __LAZYCODEMOTION_H__

#include "Unit.h"
#include "ValueNumbering.h"
struct  Edge{
    BasicBlock *src;
    BasicBlock *dst;
    bool operator<(const Edge& e) const {
        return src->getNo() < e.src->getNo() || (src->getNo() == e.src->getNo() && dst->getNo() < e.dst->getNo());
    }
    bool operator==(const Edge& e) const {
        return src->getNo() == e.src->getNo() && dst->getNo() == e.dst->getNo();
    }
};
//unordered_map is more efficient than map,but requires hash function


class LazyCodeMotion  {
    Unit* unit;
    std::unordered_map<std::string,Operand*>& htable;

    std::unordered_map<BasicBlock*, std::set<Operand*>> deexpr;
    std::unordered_map<BasicBlock*, std::set<Operand*>> ueexpr;
    std::unordered_map<BasicBlock*, std::set<Operand*>> killexpr;

    std::unordered_map<BasicBlock*, std::set<Operand*>> availout;
    std::unordered_map<BasicBlock*, std::set<Operand*>> antin;
    std::unordered_map<BasicBlock*, std::set<Operand*>> antout;
    std::map<Edge, std::set<Operand*>> earliest;
    std::map<Edge, std::set<Operand*>> later;
    std::unordered_map<BasicBlock*, std::set<Operand*>> laterin;

    std::map<Edge, std::set<Operand*>> insertset;
    std::unordered_map<BasicBlock*, std::set<Operand*>> deleteset;
    
    std::string getOpString (Instruction *inst);
    std::unordered_map<Function*,std::set<Operand*>> allexpr;
    void printAnt();
    void printLoal();
    void printall();
    void printLater();
    void collectAllexpr();
    void computeLocal();
public:
  LazyCodeMotion(Unit *unit, ValueNumbering *vn) : unit(unit),htable(vn->getmap()) {}
  void computeAvail();
  void computeAnt();
  void computeEarliest();
  void computeLater();
  void rewrite();
  void pass();
};


#endif