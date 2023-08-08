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
        if(inst->isLoad() || inst->isStore()) return nullptr;
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
            base = the_other(phi,modi->getDef());
            mod = the_other(modi,stepi->getDef());
        }
        else
            base = the_other(phi,stepi->getDef());
        
        if(def_in_loop.count(step))
            continue;
        //%t3 = add i32 %t24, %t24,这种可以在cse中简化
        inductions[the_other(phi,base)] = {base,step,step_op,mod};
        // fprintf(stderr,"induction:%s\n",the_other(phi,base)->toStr().c_str());
        // fprintf(stderr,"base:%s\n",base->toStr().c_str());
        // fprintf(stderr,"step:%s\n",step->toStr().c_str());
        // fprintf(stderr,"step_op:%d\n",step_op);
        // fprintf(stderr,"mod:%s\n",mod?mod->toStr().c_str():"null");
    }
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
    for(auto i=body->begin();i!=body->end();i=i->getNext())
        if(!i->hasNoDef())
            def_in_loop.insert(i->getDef());
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
    CondBrInstruction* br = dynamic_cast<CondBrInstruction*>(cond->getNext());

    BasicBlock* exit_bb = br->getTrueBranch()==body?br->getFalseBranch():br->getTrueBranch();

    // 现在induction增量为1的情况
    if(inductions[ctrl_val].step->getEntry()->isConstant()){
        if(inductions[ctrl_val].step->getEntry()->getValue()!=1) return;
    }
    //目前只处理等差数列
    if(!inductions.count(exit_var) || inductions[exit_var].op!=BinaryInstruction::ADD) return;

    fprintf(stderr,"body:%d\n",body->getNo());
    
    fprintf(stderr,"ctrl_val:%s\n",ctrl_val->toStr().c_str());
    fprintf(stderr,"bound:%s\n",bound->toStr().c_str());
    fprintf(stderr,"step:%s\n",inductions[ctrl_val].step->toStr().c_str());
    fprintf(stderr,"base:%s\n",inductions[ctrl_val].base->toStr().c_str());

    fprintf(stderr,"exit_var:%s\n",exit_var->toStr().c_str());
    fprintf(stderr,"base:%s\n",inductions[exit_var].base->toStr().c_str());
    fprintf(stderr,"step:%s\n",inductions[exit_var].step->toStr().c_str());

    //删除所有指令
    Instruction* dummy = body->end();
    while(dummy->getNext()!=dummy){
        Instruction* inst = dummy->getNext();
        body->remove(inst);
        delete inst;
    }
    for(auto succ_it=body->succ_begin();succ_it!=body->succ_end();succ_it++){
        auto succ = *succ_it;
        succ->removePred(body);
    }
    body->CleanSucc();
    body->addSucc(exit_bb);
    exit_bb->addPred(body);

    // base + loop_time * step = exit_var
    // (base+step*times)%mod == (base%mod+step*times%mod)%mod
    auto mulmod_type = new FunctionType(TypeSystem::intType, std::vector<Type *>{TypeSystem::intType, TypeSystem::intType, TypeSystem::intType});
    // 1. 生成循环次数
    Operand* loop_time = new Operand(new TemporarySymbolEntry(TypeSystem::intType,SymbolTable::getLabel()));
    new BinaryInstruction(BinaryInstruction::SUB,loop_time,bound,inductions[ctrl_val].base,body);
    //2. 计算循环结果
    Operand* tmp = new Operand(new TemporarySymbolEntry(TypeSystem::intType,SymbolTable::getLabel()));
    if(inductions[exit_var].modulo){
        new FuncCallInstruction(tmp,std::vector<Operand*>{loop_time,inductions[exit_var].step,inductions[exit_var].modulo},
        new IdentifierSymbolEntry(mulmod_type,"mulmod",0),body);
    }
    else
        new BinaryInstruction(BinaryInstruction::MUL,tmp,inductions[exit_var].step,loop_time,body);
    if(inductions[exit_var].modulo){
        Operand* base_mod = new Operand(new TemporarySymbolEntry(TypeSystem::intType,SymbolTable::getLabel()));
        new BinaryInstruction(BinaryInstruction::MOD,base_mod,inductions[exit_var].base,inductions[exit_var].modulo,body);
        inductions[exit_var].base = base_mod;
    }
    new BinaryInstruction(BinaryInstruction::ADD,exit_var,inductions[exit_var].base,tmp,body);
    //3. 跳转
    new UncondBrInstruction(exit_bb,body);
}
