#ifndef __GVNPRE_H__
#define __GVNPRE_H__

#include <set>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <string>

#include "Unit.h"

/*
TODO :
1. build antic using postorder traversal
2. speed up exprset rplc
*/
namespace ExprOp
{
    enum
    {
        TEMP,
        ADD,
        SUB,
        MUL,
        DIV,
        MOD,
        PHI,
        GEP
    };
};
#define ValueNr Operand *
class Expr
{
    unsigned opcode;
    std::vector<Operand *> operands;
    std::string strhash;

public:
    Expr() {}
    Expr(unsigned op, std::vector<Operand *> ops) : opcode(op), operands(ops)
    {
        switch (opcode)
        {
        case ExprOp::TEMP:
            strhash = "TEMP";
            break;
        case ExprOp::ADD:
            strhash = "ADD";
            break;
        case ExprOp::SUB:
            strhash = "SUB";
            break;
        case ExprOp::MUL:
            strhash = "MUL";
            break;
        case ExprOp::DIV:
            strhash = "DIV";
            break;
        case ExprOp::MOD:
            strhash = "MOD";
            break;
        case ExprOp::GEP:
            strhash = "GEP";
            break;
        default:
            assert(0);
        }
        for (auto operand : operands)
            strhash += "," + operand->toStr();
    }
    Expr(Operand *temp)
    {
        opcode = ExprOp::TEMP;
        operands.push_back(temp);
        strhash = temp->toStr();
    }
    ~Expr() {}
    unsigned getOpcode() const { return opcode; }
    const std::vector<Operand *> &getOperands() const { return operands; }
    std::string tostr() const { return strhash; }
    bool operator==(const Expr &expr) const
    {
        return tostr() == expr.tostr();
    }
    bool operator!=(const Expr &expr) const
    {
        return tostr() != expr.tostr();
    }
    bool operator<(const Expr &expr) const
    {
        return tostr() < expr.tostr();
    }
};
struct Ehash
{
    size_t operator()(const Expr &expr) const
    {
        return std::hash<std::string>()(expr.tostr());
    }
};

static std::unordered_map<Expr, Operand *, Ehash> htable;
static std::map<std::pair<int, int>, std::unordered_map<ValueNr, ValueNr>> trans_cache;
#define lookup(expr) (htable.count(expr) ? htable[expr] : nullptr)

class Exprset
{
    // TODO:
    // most set is value canonical (i.e. each value has only one expr), we can
    // further speed it up
    std::set<Expr> exprs;
    std::unordered_map<ValueNr, std::set<Expr>> val2exprs;
    std::vector<Expr> topological_seq;
    bool changed = true; // if changed after compute topological_seq last time
public:
    std::unordered_map<ValueNr, Operand *> leader_map;
    // iterator for exprs
    typedef std::set<Expr>::iterator iterator;
    iterator begin() { return exprs.begin(); }
    iterator end() { return exprs.end(); }

    void insert(Expr expr)
    {
        assert(htable.count(expr));
        exprs.insert(expr);
        val2exprs[htable[expr]].insert(expr);
        changed = true;

        // if(expr.getOpcode()==ExprOp::TEMP)
        //     leader_map[htable[expr]] = expr.getOperands()[0];
    }
    void erase(Expr expr)
    {
        assert(htable.count(expr));
        exprs.erase(expr);
        val2exprs[htable[expr]].erase(expr);
        if (val2exprs[htable[expr]].empty())
            val2exprs.erase(htable[expr]);
        changed = true;
    }
    void vinsert(Expr expr)
    {
        if (htable.count(expr) == 0)
        {
            for (auto [e, v] : htable)
                printf("%s : %s\n", e.tostr().c_str(), v->toStr().c_str());
            assert(htable.count(expr));
        }
        if (!val2exprs.count(htable[expr]))
        {
            exprs.insert(expr);
            val2exprs[htable[expr]].insert(expr);
            changed = true;
        }
    }
    void vrplc(Expr expr)
    {
        ValueNr val = lookup(expr);
        if (val2exprs.count(val) != 0)
        {
            for (auto e : val2exprs[val])
                exprs.erase(e);
            val2exprs[val].clear();
        }
        insert(expr);
        changed = true;
    }
    Operand *find_leader(ValueNr val)
    {
        // global , parameter, constant is available anywhere instinctly,
        // but should have been added when building sets, but not available in every block
        if (val->getEntry()->isConstant() || val->getEntry()->isVariable())
            return val;

        if (!val || val2exprs.count(val) == 0)
            return nullptr;
        assert(val2exprs.count(val) <= 1);
        if (val2exprs.count(val))
        {
            auto e = *val2exprs[val].begin();
            // assert(e.getOpcode()==ExprOp::TEMP);
            return e.getOperands()[0];
        }
        return nullptr;
    }
    void clear()
    {
        val2exprs.clear();
        exprs.clear();
        changed = true;
    }
    bool empty() const { return exprs.empty(); }
    bool operator==(const Exprset &set) const
    {
        return exprs == set.exprs;
    }
    bool operator!=(const Exprset &set) const
    {
        return exprs != set.exprs;
    }
    std::vector<Expr> topological_sort();
    std::set<Expr> &getExprs() { return exprs; }
    std::unordered_map<ValueNr, std::set<Expr>> &getValnrs() { return val2exprs; }
};

// #define DEBUG_GVNPRE

class GVNPRE
{
    Unit *unit;

public:
    GVNPRE(Unit *unit) : unit(unit) {}
    void pass();
};

class GVNPRE_FUNC
{
    int expr_cnt, phi_cnt, erase_cnt;
    Function *func;

    std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> domtree;

    std::unordered_map<BasicBlock *, Exprset> expr_gen;
    std::unordered_map<BasicBlock *, Exprset> phi_gen;
    std::unordered_map<BasicBlock *, Exprset> tmp_gen;

    std::unordered_map<BasicBlock *, Exprset> new_sets;

    std::unordered_map<BasicBlock *, Exprset> avail_out;
    std::unordered_map<BasicBlock *, Exprset> antic_in;

    Instruction *genInst(Operand *dst, unsigned op, std::vector<Operand *> leaders);
    Instruction *genInst(Operand *dst, unsigned op, std::map<BasicBlock *, Operand *> phiargs);

    Exprset phi_trans(Exprset set, BasicBlock *from, BasicBlock *to);
    Expr phi_trans(Expr expr, BasicBlock *from, BasicBlock *to);
    void clean(Exprset &set);
    Operand *gen_fresh_tmep(Expr e);

    void rmcEdge(Function *func);
    void addGval(Function *func);
    void buildDomTree(Function *func);

    void buildSets(Function *func);
    void buildAntic(Function *func);
    void insert(Function *func);
    void elminate(Function *func);

public:
    GVNPRE_FUNC(Function *f) : func(f) { expr_cnt = phi_cnt = erase_cnt = 0; }
    void pass();
};

#endif