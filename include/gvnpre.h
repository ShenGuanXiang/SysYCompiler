#ifndef __GVNPRE_H__
#define __GVNPRE_H__

#include<set>
#include<vector>
#include<string>
#include<functional>
#include<unordered_map>

#include "Unit.h"

#define ValueNr Operand*

/*
TODO : 
1. use std::string to replace struct expr
2. add to avail_out dynamically, rather than recalculate it every time
*/

class Expr{
public:
    enum{ADD=1,SUB,MUL,DIV,MOD,PHI};
    std::vector<Operand*>vals;
    unsigned op;
    Expr() :op(0) {}
    Expr(Operand* val) :op(0) {vals.push_back(val);}
    bool operator== (const Expr& expr) const{
        if(op!=expr.op || vals.size()!=expr.vals.size()) return false;
        //commutative for simple arithmetic operations
        if(op>=ADD && op<=MOD) {return (vals[0]==expr.vals[0] && vals[1]==expr.vals[1]) || (vals[0]==expr.vals[1] && vals[1]==expr.vals[0]);}
        for(size_t i=0;i<vals.size();i++)
            if(vals[i]!=expr.vals[i]) return false;
        return true;
    }
    bool operator!= (const Expr& expr) const{return !(*this==expr);}
    std::string tostr(){
        char opstr[]={' ','+','-','*','/','%'};
        std::string exprstr;
        if(vals.size()==2){
            exprstr = vals[0]->toStr() + opstr[op] + vals[1]->toStr();
        }
        else if(vals.size()==1)
            exprstr = vals[0]->toStr();
        return exprstr;
    }
};

typedef std::unordered_map<Operand*,Expr> Exprset;


// #define DEBUG_GVNPRE

void logf(const char* formmat,...);
void printset(Exprset set);



std::vector<Expr> topological(Exprset set);

namespace std{
    template<>
    struct hash<Expr>{
        size_t operator() (const Expr& expr) const{
            if(!expr.op) return std::hash<void*>()(expr.vals[0]);
            size_t hvals=0;
            for(auto val : expr.vals)
                hvals^=std::hash<void*>()(val);
            return hvals ^ std::hash<unsigned>()(expr.op);
        }
    };
}

//typedef std::vector<std::pair<Operand*,std::string>> Exprset;


class GVNPRE{
    std::unordered_map<Expr,Operand*>htable; //used to store expression
    Unit* unit;

    std::unordered_map<BasicBlock*,std::vector<BasicBlock*>>domtree;
    std::unordered_map<int,Expr> int2Expr;//used to map constant operands to values
    std::unordered_map<float,Expr> float2Expr;//used to map constant operands to values
    

    std::unordered_map<BasicBlock*,Exprset>expr_gen;
    std::unordered_map<BasicBlock*,Exprset>phi_gen;
    std::unordered_map<BasicBlock*,Exprset>tmp_gen;

    std::set<Operand*>kill;
    std::unordered_map<BasicBlock*,Exprset> new_sets;
    
    std::unordered_map<BasicBlock*,Exprset> avail_out;
    std::unordered_map<BasicBlock*,Exprset> antic_in;

    Expr genExpr(Instruction* inst);
    Expr genExpr(Operand* temp);
    Instruction* genInst(Operand* dst,unsigned op,std::vector<Operand*>leaders);
    Instruction* genInst(Operand* dst,unsigned op,std::map<BasicBlock*,Operand*>phiargs);
    

    Operand* lookup(Expr expr);
    void vinsert(Exprset& set,Expr expr);
    Exprset phi_trans(Exprset set,BasicBlock* from,BasicBlock* to);
    Expr phi_trans(Expr expr,BasicBlock* from,BasicBlock* to);
    void clean(Exprset& set);
    Operand* find_leader(Exprset set, Operand* val);
    Operand* gen_fresh_tmep(Expr e);
    

    void rmcEdge(Function* func);
    
    void buildDomTree(Function* func);

    void gvnpre(Function* func);
    void buildSets(Function* func);
    void buildAntic(Function* func);
    void insert(Function* func);
    void elminate(Function* func);

public:

    GVNPRE(Unit* u) : unit(u) {}
    void pass();
};

#endif