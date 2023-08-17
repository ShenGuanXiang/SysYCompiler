#include "Multithread.h"
#include <algorithm>
#include <queue>
static std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> domtree;

static inline Operand* new_const_op(int val){
    return new Operand(new ConstantSymbolEntry(TypeSystem::constIntType,val));
}

static inline Operand* new_int_temp(){
    return new Operand(new TemporarySymbolEntry(TypeSystem::intType,SymbolTable::getLabel()));
}

static inline Operand* new_bool_temp(){
    return new Operand(new TemporarySymbolEntry(TypeSystem::boolType,SymbolTable::getLabel()));
}

static inline Operand* copy_op(Operand* op){
    auto sym_entry = op->getEntry();
    if(sym_entry->isConstant() || sym_entry->isVariable())
        return op;
    return new Operand(new TemporarySymbolEntry(sym_entry->getType(),SymbolTable::getLabel()));
}

static inline void call_lock(Operand* mutex,BasicBlock*bb){
    auto lc_func = new FunctionType(TypeSystem::voidType, std::vector<Type *>{TypeSystem::intType});
    auto lock_inst = new FuncCallInstruction(new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())),
                        std::vector<Operand *>{mutex},
                        new IdentifierSymbolEntry(lc_func, "__lock", 0),
                        bb);
}

static inline void call_unlock(Operand* mutex,BasicBlock*bb){
    auto lc_func = new FunctionType(TypeSystem::voidType, std::vector<Type *>{TypeSystem::intType});
    new FuncCallInstruction(new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())),
                        std::vector<Operand *>{mutex},
                        new IdentifierSymbolEntry(lc_func, "__unlock", 0),
                        bb);
}

static inline void call_join(Operand* no,BasicBlock*bb){
    auto jt_func = new FunctionType(TypeSystem::voidType, std::vector<Type *>{TypeSystem::intType});
    new FuncCallInstruction(new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())),
                            // 如果参数为 0，执行系统调用 SYS_exit 来退出线程。不为 0 则会执行系统调用 SYS_waitid 来等待并回收线程资源。
                            std::vector<Operand *>{no}, // 或者 1
                            new IdentifierSymbolEntry(jt_func, "__join_threads", 0),
                            bb);
}

static inline Operand* new_global_int(Unit* u,std::string name){
    // WARNING：注意全局变量重名的情况
    // 下面的代码copy的，变量名没有意义
    auto _mutex = new IdentifierSymbolEntry(TypeSystem::intType, name, 0); // 或许name前面还需要加点啥，遇到了再说
    SymbolEntry *addr_se_mu = new IdentifierSymbolEntry(*_mutex);
    addr_se_mu->setType(new PointerType(_mutex->getType()));
    auto addr_mu = new Operand(addr_se_mu);
    _mutex->setAddr(addr_mu);
    u->insertDecl(_mutex);
    return new Operand(_mutex);
}

static inline Operand* call_create(BasicBlock*bb){
    Operand* ret = new_int_temp();
    auto ct_func = new FunctionType(TypeSystem::intType, std::vector<Type *>{nullptr});
    new FuncCallInstruction(ret,std::vector<Operand *>{},new IdentifierSymbolEntry(ct_func, "__create_threads", 0),bb);
}

static inline BasicBlock* get_exit(LoopInfo loop){
    auto exiting = loop.loop_exiting_block;
    auto last_inst = exiting->end()->getPrev();
    assert(last_inst->isCond() || last_inst->isUncond());
    if(last_inst->isUncond())
        return dynamic_cast<UncondBrInstruction*>(last_inst)->getBranch();
    if(last_inst->isCond()){
        auto cond_inst = dynamic_cast<CondBrInstruction*>(last_inst);
        auto true_bb = cond_inst->getTrueBranch();
        auto false_bb = cond_inst->getFalseBranch();
        if(std::find(loop.loop_blocks.begin(),loop.loop_blocks.end(),true_bb)!=loop.loop_blocks.end())
            return false_bb;
        else
            return true_bb;
    }
}

//是否需要考虑修改phi？
template<typename P>
static void rplc_target(BasicBlock*bb,P pred,BasicBlock*new_bb){
    // 将bb中的跳转语句中的目标替换
    // 将语句中满足pred谓词的块进行替换
    auto last_inst = bb->end()->getPrev();
    BasicBlock* old_target;
    if(!last_inst->isCond() && !last_inst->isUncond())
        return;
    if(last_inst->isUncond()){
        auto br = dynamic_cast<UncondBrInstruction*>(last_inst);
        if(!pred(br->getBranch()))
            return;
        old_target = br->getBranch();
        br->setBranch(new_bb);
    }
    else if(last_inst->isCond()){
        auto cond_inst = dynamic_cast<CondBrInstruction*>(last_inst);
        auto true_bb = cond_inst->getTrueBranch();
        auto false_bb = cond_inst->getFalseBranch();
        if(!pred(true_bb) && !pred(false_bb))
            return;
        // 只有一个满足pred
        assert(pred(true_bb) ^ pred(false_bb));
        else if(pred(true_bb)){
            cond_inst->setTrueBranch(new_bb);
            old_target = true_bb;
        }
        else{
            cond_inst->setFalseBranch(new_bb);
            old_target = false_bb;
        }
    }
    // 发生指令替换以后，还需要修改前驱后继关系
    bb->removeSucc(pred);
    bb->addSucc(new_bb);
    pred->removePred(bb);
    new_bb->addPred(bb);
}


// 注意用这种方式复制一整个循环应该按支配顺序复制
static void copy_bb(BasicBlock*from, BasicBlock*to, std::unordered_map<Operand*,Operand*>&old2copy_op, std::unordered_map<BasicBlock*,BasicBlock*>&old2copy_bb){
    // 保存循环体内定义的变量
    for(auto inst = from->begin();inst!=from->end();inst=inst->getNext()){
        Instruction* inst_copy = inst->copy();
        for(auto& u:inst_copy->getUses())
            if(old2copy_op.count(u))
                u = old2copy_op[u];
        inst_copy->setParent(to);
        if(!inst_copy->hasNoDef()){
            Operand* old_def_op = inst_copy->getDef();
            old2copy_op[old_def_op] = copy_op(old_def_op);
            inst_copy->setDef(old2copy_op[old_def_op]);
            // 这里认为没有变量满足：在循环体内定义，在循环体外使用，因此也不会引入phi
        }
        if(inst_copy->isPHI()){
            auto phi_inst = dynamic_cast<PhiInstruction*>(inst_copy);
            std::set<BasicBlock*>torm;
            for(const auto& p:phi_inst->getSrcs()){
                if(old2copy_bb.count(p.first))
                    torm.insert(p.first);
            }
            for(auto bb:torm){
                phi_inst->removeEdge(bb);
                phi_inst->addEdge(old2copy_bb[bb],old2copy_op[phi_inst->getSrcs()[bb]]);
            }
        }
        to->insertBack(inst_copy);
    }
}

static LoopInfo copy_loop(LoopInfo src_loop,std::pair<Operand*,Operand*>new_range){
    //这里对range的考虑还不完善，直接拿来用了
    std::vector<BasicBlock*> copy_seq; // in order of dominance
    std::unordered_map<Operand*,Operand*> old2copy_op;
    std::unordered_map<BasicBlock*,BasicBlock*> old2copy_bb;
    std::queue<BasicBlock*>q;
    copy_seq.push_back(src_loop.loop_header);
    q.push(src_loop.loop_header);
    while (!q.empty()){
        auto t = q.front();
        q.pop();
        for(auto bb:domtree[t]){
            copy_seq.push_back(bb);
            q.push(bb);
        }
    }
    for(auto& bb:copy_seq){
        BasicBlock* bb_copy = new BasicBlock(bb->getParent());
        old2copy_bb[bb] = bb_copy;
        bb=bb_copy;
        copy_bb(bb,bb_copy,old2copy_op,old2copy_bb);
    }
    // copy_seq中的bb已经全部替换为copy_bb
    // 修改br指令中的跳转目标
    for(auto bb:copy_seq){
        rplc_target(bb,[&](BasicBlock*src){return old2copy_bb.count(src);},old2copy_bb[bb]);
    }
    LoopInfo ret = src_loop;
    ret.loop_header = old2copy_bb[src_loop.loop_header];
    ret.loop_exiting_block = old2copy_bb[src_loop.loop_exiting_block];
    ret.loop_blocks.clear();
    std::copy(copy_seq.begin(),copy_seq.end(),std::back_inserter(ret.loop_blocks));
    ret.indvar_range = new_range;
    ret.indvar = old2copy_op[src_loop.indvar];
    for(auto use_inst : ret.indvar->getUses()){
        if(use_inst->isPHI()){
            //这里应该在
        }
        
    }
    return ret;
}

void Multithread::insert_opt_jump(BasicBlock* new_header)
{
    // 这里我们先认为循环的header只有一个循环外的前驱
    std::set<BasicBlock*> preds;
    std::copy(loop.loop_header->pred_begin(),loop.loop_header->pred_end(),std::inserter(preds,preds.end()));
    std::set<BasicBlock*> torm;
    for(auto pred : preds){
        if(std::find(loop.loop_blocks.begin(),loop.loop_blocks.end(),pred)!=loop.loop_blocks.end())
            continue;
        torm.insert(pred);
    }
    for(auto pred:torm){
        preds.erase(pred);
    }
    assert(preds.size()==1);
    const auto& pred = *preds.begin();

    //header中可能会有phi
    
    // 这里有点问题。。
    BasicBlock* judge_bb = new BasicBlock(loop.loop_header->getParent());
    Operand* loop_span_op = compute_loop_span(loop,judge_bb);
    Operand* cmp_res_op = new_bool_temp();
    new CmpInstruction(CmpInstruction::LE,cmp_res_op,new_const_op(nr_threads),loop_span_op,judge_bb);
    new CondBrInstruction(loop.loop_header,new_header,cmp_res_op,judge_bb);
    // 这里header如果有多个循环外的前继？甚至可能有很多phi？？
    rplc_target(pred,[&](BasicBlock*_b){return _b==loop.loop_header;},judge_bb);
    judge_bb->addSucc(loop.loop_header);
    judge_bb->addSucc(new_header);
    loop.loop_header->addPred(judge_bb);
    new_header->addPred(judge_bb);
    // 修正phi
    for(auto inst = loop.loop_header->begin();inst!=loop.loop_header->end();inst=inst->getNext()){
        if(inst->isPHI()){
            auto phi_inst = dynamic_cast<PhiInstruction*>(inst);
            for(const auto& p:phi_inst->getSrcs()){
                if(p.first==pred){
                    phi_inst->removeEdge(pred);
                    phi_inst->addEdge(judge_bb,p.second);
                    break;
                }
            }
        }
    }
    for(auto inst = new_header->begin();inst!=new_header->end();inst=inst->getNext()){
        if(inst->isPHI()){
            auto phi_inst = dynamic_cast<PhiInstruction*>(inst);
            for(const auto& p:phi_inst->getSrcs()){
                if(p.first==pred){
                    phi_inst->removeEdge(pred);
                    phi_inst->addEdge(judge_bb,p.second);
                    break;
                }
            }
        }
    }

    
    
}
Operand* compute_loop_span(LoopInfo loop,BasicBlock*bb)
{
    Operand* loop_span_op = new_int_temp();
    switch(loop.cmpType){
        case CmpInstruction::L:
            new BinaryInstruction(BinaryInstruction::SUB,loop_span_op,loop.indvar_range.second,loop.indvar_range.first,judge_bb);
            break;
        case CmpInstruction::G:
            new BinaryInstruction(BinaryInstruction::SUB,loop_span_op,loop.indvar_range.first,loop.indvar_range.second,judge_bb);
            break;
        case CmpInstruction::LE:{
            Operand* tmp = new_int_temp();
            new BinaryInstruction(BinaryInstruction::SUB,tmp,loop.indvar_range.second,loop.indvar_range.first,judge_bb);
            new BinaryInstruction(BinaryInstruction::ADD,loop_span_op,tmp,new_const_op(1),judge_bb);
            break;
        }
        case CmpInstruction::GE:{
            Operand* tmp = new_int_temp();
            new BinaryInstruction(BinaryInstruction::SUB,tmp,loop.indvar_range.first,loop.indvar_range.second,judge_bb);
            new BinaryInstruction(BinaryInstruction::ADD,loop_span_op,tmp,new_const_op(1),judge_bb);
            break;
        }
        default:
            assert(false);
    }
    return loop_span_op;
}

void Multithread::compute_domtree()
{
    domtree.clear();
    auto func = loop.loop_header->getParent();
    func->ComputeDom();
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        BasicBlock *bb = *it_bb;
        if (bb == func->getEntry())
            continue;
        domtree[bb->getIDom()].push_back(bb);
    }
}


void Multithread::transform()
{
    compute_domtree();
    // 1. calculate iterate times
    // 2. calculate the range of each thread
    BasicBlock* pre_header = new BasicBlock(loop.loop_header->getParent());
    insert_opt_jump(pre_header);

    std::vector<Operand*> left_ends(nr_threads);
    Operand* nr_threads_op = new_const_op(nr_threads);
    Operand* len = compute_loop_span(loop,pre_header);
    for(auto &op:left_ends)
        op = new_int_temp();
    for(int i=0;i<nr_threads;i++){
        //目前只考虑[left,right) i++ 的情况
        Operand* div_res_op = new_int_temp();
        new BinaryInstruction(BinaryInstruction::DIV,div_res_op,len,nr_threads_op,pre_header);
        Operand* mul_res_op = new_int_temp();
        new BinaryInstruction(BinaryInstruction::MUL,mul_res_op,div_res_op,new_const_op(i),pre_header);
        new BinaryInstruction(BinaryInstruction::ADD,left_ends[i],mul_res_op,loop.indvar_range.first,pre_header);
    }
    
    Unit* u = pre_header->getParent()->getParent();
    Operand* mutex = new_global_int(u,"_mutex_"+std::to_string(pre_header->getNo()));
    Operand* barrier = new_global_int(u,"_barrier_"+std::to_string(pre_header->getNo()));
    call_lock(mutex,pre_header);
    new BinaryInstruction(BinaryInstruction::SUB,barrier,nr_threads_op,new_const_op(1),pre_header);
    call_unlock(mutex,pre_header);

    int nr_create_call = nr_threads-1;
    while(nr_create_call){
        LoopInfo loop_copy = copy_loop(loop);
        BasicBlock* header_copy = loop_copy.loop_header;
        BasicBlock* exiting_copy = loop_copy.loop_exiting_block;
        Operand* cmp_res_op = new_bool_temp();
        BasicBlock* create_bb = new BasicBlock(pre_header->getParent());

        Operand* ret = call_create(pre_header);
        new CmpInstruction(CmpInstruction::E,cmp_res_op,ret,new_const_op(0),pre_header);
        new CondBrInstruction(create_bb,header_copy,cmp_res_op,pre_header);

        BasicBlock* exit_bb = new BasicBlock(pre_header->getParent());
        call_lock(mutex,exit_bb);
        new BinaryInstruction(BinaryInstruction::SUB,barrier,barrier,new_const_op(1),exit_bb);
        call_unlock(mutex,exit_bb);
        call_join(new_const_op(1),exit_bb);
        //这里有个假设需要验证：exiting的目标，一个是循环内的块，另一个是循环外的块（exit block）
        BasicBlock* old_exit_bb = get_exit(loop_copy);
        // 由于exit在循环外部，因此loop_copy中的exting块应该还指向原来的exit块
        assert(std::find(exiting_copy->succ_begin(),exiting_copy->succ_end(),old_exit_bb)!=exiting_copy->succ_end());
        // 替换exiting块的目标
        rplc_target(exiting_copy,[&](BasicBlock*_b){return _b==old_exit_bb;},exit_bb);
        

        pre_header = create_bb;
        nr_create_call--;
    }
    //主线程的代码
    LoopInfo loop_copy = copy_loop(loop);
    new UncondBrInstruction(loop_copy.loop_header,pre_header);
    BasicBlock* old_exit_bb = get_exit(loop);

    BasicBlock* main_exit = new BasicBlock(pre_header->getParent());
    call_join(new_const_op(0),main_exit);
    Operand* cmp_res_op = new_bool_temp();
    new CmpInstruction(CmpInstruction::NE,cmp_res_op,barrier,new_const_op(0),main_exit);
    new CondBrInstruction(main_exit,old_exit_bb,cmp_res_op,main_exit);

    rplc_target(loop.loop_exiting_block,[&](BasicBlock*_b){return _b==old_exit_bb;},main_exit);
    
    // 3. copy loop code for each thread
    // 4. add multi thread branch
}
