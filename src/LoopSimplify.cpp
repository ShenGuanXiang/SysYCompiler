#include "LoopSimplify.h"
#include <algorithm>

Operand* LoopSimplify::checkForm(BasicBlock *bb)
{
    //1. 基本块本身是一个循环
    bool self_loop = false;
    for(auto succ_it = bb->succ_begin();succ_it!=bb->succ_end();succ_it++){
        if(*succ_it == bb){
            self_loop = true;
            break;
        }
    }
    if(!self_loop)
        return nullptr;
    //2. 只有一个变量，定义在循环体内，在循环体外被使用，phi只有两个参数
    Operand* exit_var = nullptr;
    for(auto inst = bb->begin();inst!=bb->end();inst=inst->getNext()){
        if(inst->isCond()||inst->isUncond()) continue;
        if(inst->hasNoDef()) return nullptr; //没有定义的指令一般有副作用
        Operand* def = inst->getDef();
        for(auto i : def->getUses()){
            BasicBlock* use_bb = i->getParent();
            if(use_bb!=bb){
                if(!exit_var) exit_var = def;
                else if(exit_var!=def) return nullptr;
            }
            if(inst->isPHI() && dynamic_cast<PhiInstruction*>(inst)->getSrcs().size()!=2){
                return nullptr;
            }
        }
    }
    return exit_var;
}

void LoopSimplify::pass()
{
    for(auto func_it = unit->begin();func_it!=unit->end();func_it++){
        for(auto bb_it = (*func_it)->begin();bb_it!=(*func_it)->end();bb_it++){
            if(Operand*dst=checkForm(*bb_it)){
                SimpleLoop sp(*bb_it,dst);
                sp.simplify();
            }
        }
    }
}
static inline Operand* op1(Instruction* i){return i->getUses()[0];}
static inline Operand* op2(Instruction* i){return i->getUses()[1];}
static inline Operand* the_other(Instruction* i,Operand* op){
    return op==op1(i)?op2(i):op1(i);
}
void SimpleLoop::findInduction()
{
    std::set<Instruction*>phis;
    for(auto inst = body->begin();inst!=body->end();inst=inst->getNext()){
        if(inst->isPHI()){
            phis.insert(inst);
        }
        else break;
    }
    for(auto phi : phis){
        std::stack<Operand*> path;
        if(!dfs(phi,path)) continue;
        Instruction* last_phi = path.top()->getDef();
        path.pop();
        if(last_phi != phi) continue;
        if(path.size()>2) continue;

        Instruction* stepi = path.top()->getDef();path.pop();
        unsigned step_op = stepi->getOpcode();
        Operand* step = the_other(stepi,phi->getDef());
        Operand* base;

        Operand* mod = nullptr;
        if(!path.empty()){
            Instruction* modi = path.top()->getDef();path.pop();
            base = the_other(modi,modi->getDef());
            mod = the_other(modi,stepi->getDef());
        }
        else
            base = the_other(phi,stepi->getDef());
        inductions[the_other(phi,base)] = {base,step,step_op,mod};
        fprintf(stderr,"base:%s\n",base->toStr().c_str());
        fprintf(stderr,"step:%s\n",step->toStr().c_str());
        fprintf(stderr,"step_op:%d\n",step_op);
        fprintf(stderr,"mod:%s\n",mod?mod->toStr().c_str():"null");
    }
}

Operand *SimpleLoop::gen_loop_time()
{
    Instruction* cond;
    for(auto inst=body->rbegin();inst!=body->rend();inst=inst->getPrev())
        if(inst->isCmp()){
            cond = inst;
            break;
        }
    Operand* bound=nullptr;
    Operand* op1 = cond->getUses()[0];
    Operand* op2 = cond->getUses()[1];
    // find in inductions vector if op1 or op2 is an induction variable
    // std::find()
}

bool SimpleLoop::dfs(Instruction *i, std::stack<Operand *>& path)
{
    if(i->getParent()!=body) return false;
    if(i->isPHI()&&!path.empty()) {
        return true;
    }
    for(auto u : i->getUses()){
        if(u->getEntry()->isVariable()||u->getEntry()->isConstant()) continue;
        path.push(u);
        if(dfs(u->getDef(),path)) return true;
        path.pop();
    }
    return false;
}

void SimpleLoop::simplify()
{
    findInduction();
    CmpInstruction* cond = nullptr;
    for(auto inst=body->rbegin();inst!=body->rend();inst=inst->getPrev()){
        if(inst->isCmp()){
            cond = dynamic_cast<CmpInstruction*>(inst);
            break;
        }
    }
    if(!cond) return;
    Operand* ctrl_val = nullptr;
    Operand* bound = nullptr;
    for(auto u : cond->getUses())
        if(inductions.count(u)){
            ctrl_val = u;
            bound = the_other(cond,ctrl_val);
            break;
        }
    if(!ctrl_val) return;

    // 现在induction增量为1的情况
    if(inductions[ctrl_val].step->getEntry()->isConstant()){
        if(inductions[ctrl_val].step->getEntry()->getValue()!=1) return;
    }

    
    fprintf(stderr,"ctrl_val:%s\n",ctrl_val->toStr().c_str());
    fprintf(stderr,"bound:%s\n",bound->toStr().c_str());
    fprintf(stderr,"step:%s\n",inductions[ctrl_val].step->toStr().c_str());
    fprintf(stderr,"base:%s\n",inductions[ctrl_val].base->toStr().c_str());

    fprintf(stderr,"exit_var:%s\n",exit_var->toStr().c_str());
    fprintf(stderr,"base:%s\n",inductions[exit_var].base->toStr().c_str());
    fprintf(stderr,"step:%s\n",inductions[exit_var].step->toStr().c_str());
}
