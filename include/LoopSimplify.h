#include "Unit.h"
#include <stack>

struct Induction{
    Operand* base;
    Operand* step;
    unsigned op;
    Operand* modulo;
};
struct SimpleLoop{
    BasicBlock* body;
    Operand* exit_var;
    std::set<Operand*> def_in_loop;
    std::unordered_map<Operand*,Induction>inductions;
    bool dfs(Instruction* i,std::stack<Operand*>& path);
    void findInduction();
public:
    SimpleLoop(BasicBlock* bb,Operand* dst) : body(bb),exit_var(dst) {}
    void simplify();
};
class LoopSimplify{
    Unit* unit;
    Operand* checkForm(BasicBlock* bb);   
public:
    LoopSimplify(Unit* u) : unit(u) {}
    void pass();
};