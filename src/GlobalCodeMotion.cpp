#include "GlobalCodeMotion.h"
#include "LoopUnroll.h"

void GlobalCodeMotion::gvn(Function *func)
{
    // construct reverse post order
    std::stack<BasicBlock*> rpo;
    h.post_order(func->getEntry(),rpo);
    ComSubExprElim cse;
    while(!rpo.empty()){
        BasicBlock* bb = rpo.top();
        rpo.pop();
        cse.pass1(bb);
    }
}

void GlobalCodeMotion::schedule_early(Instruction *inst)
{
    if(h.visited[inst]) return;
    h.visited[inst] = true;
    BasicBlock* root = inst->getParent()->getParent()->getEntry();
    schedule_block[inst] = root;
    for(const auto input : inst->getUses()){
        if(input->getEntry()->isConstant() || input->getEntry()->isVariable()) {
            continue;
        }
        else {
            Instruction* input_inst = input->getDef();
            schedule_early(input_inst);
            if(h.get_dom_depth(schedule_block[input_inst]) > h.get_dom_depth(schedule_block[inst])){
                schedule_block[inst] = schedule_block[input_inst];
            }
        }
    }
}

void GlobalCodeMotion::schedule_late(Instruction *inst)
{
    if(h.visited[inst]) return;
    h.visited[inst] = true;
    BasicBlock* lca = nullptr;
    if(inst->hasNoDef())
        return;
    const auto& uses_list = inst->getDef()->getUses();
    for(const auto use_inst : uses_list){
        schedule_late(use_inst);
        BasicBlock* use = schedule_block[use_inst];
        if(use_inst->isPHI()){
            PhiInstruction* phi = dynamic_cast<PhiInstruction*>(use_inst);
            //there can be multiple edge that use inst
            std::vector<BasicBlock*> preds;
            for(auto p:phi->getSrcs()){
                if(p.second == inst->getDef()){
                    preds.push_back(p.first);
                }
            }
            use = preds[0];
            for(size_t i=1;i<preds.size();i++){
                use = h.find_lca(use,preds[i]);
            }
        }
        lca = h.find_lca(lca,use);
    }
    if(uses_list.empty())
        lca = schedule_block[inst];
    BasicBlock* best = lca;
    if(lca->getNo()!=schedule_block[inst]->getNo())
        fprintf(stderr,"[gcm]%s:%d-%d->%d\n",inst->getDef()->toStr().c_str(),lca->getNo(),inst->getParent()->getNo(),schedule_block[inst]->getNo());
    while(lca != schedule_block[inst]->getIDom()){
        if(h.get_loop_depth(lca) < h.get_loop_depth(best)){
            best = lca;
        }
        lca = lca->getIDom();
    }
    schedule_block[inst] = best;
}

void GlobalCodeMotion::move(Instruction *inst)
{
    if(h.visited[inst]) return;
    h.visited[inst] = true;
    for(const auto input : inst->getUses()){
        if(input->getEntry()->isConstant() || input->getEntry()->isVariable()) 
            continue;
        Instruction* input_inst = input->getDef();
        move(input_inst);
    }
    BasicBlock* src = inst->getParent();
    BasicBlock* dst = schedule_block[inst];
    if(src == dst) return; 
    src->remove(inst);
    if(h.get_dom_depth(src) < h.get_dom_depth(dst)){
        dst->insertBefore(inst,h.prepend_points[dst]);
    }
    else{
        dst->insertBefore(inst,h.append_points[dst]);
    }
    move_count++;
    if(inst->isLoad())
        load_count++;
}

void GlobalCodeMotion::pass()
{
    for(auto func_it = unit->begin();func_it!=unit->end();func_it++){
        Function* func = *func_it;
        pass(func);
    }
    fprintf(stderr,"[GCM]: %u instructions moved, %u of them are LOAD\n",move_count,load_count);
}

void GlobalCodeMotion::pass(Function *func)
{
    // gvn(func);

    h.compute_info(func);

    // pin instructions
    std::set<Instruction*> pinned_insts;
    for(auto bb_it = func->begin();bb_it!=func->end();bb_it++){
        BasicBlock* bb = *bb_it;
        for(auto inst = bb->begin();inst!=bb->end();inst=inst->getNext()){
            schedule_block[inst] = inst->getParent();
            if(h.is_pinned(inst)){
                pinned_insts.insert(inst);
            }
        }
    }
    
    // schedule early
    h.clear_visited(func);
    for(auto pinned : pinned_insts){
        h.visited[pinned] = true;
    }
    for(auto pinned : pinned_insts){
        for(const auto input : pinned->getUses()){
            if(input->getEntry()->isConstant() || input->getEntry()->isVariable()) 
                continue;
            Instruction* input_inst = input->getDef();
            schedule_early(input_inst);
        }
    }

    // sanity check
    for(auto bb_it = func->begin();bb_it!=func->end();bb_it++){
        BasicBlock* bb = *bb_it;
        for(auto inst = bb->begin();inst!=bb->end();inst=inst->getNext()){
            if(!schedule_block.count(inst)){
                inst->output();
                assert(schedule_block.count(inst));
            }
        }
    }
    for(auto p : schedule_block){
        assert(p.first);
    }

    // print_schedule();
    // schedule late
    h.clear_visited(func);
    for(auto pinned : pinned_insts){
        h.visited[pinned] = true;
    }
    // following code is used to remove pinned load, but not correct 
    // std::vector<Instruction*> rm_list;
    // for(auto pinned : pinned_insts){
    //     if(pinned->isLoad())
    //         rm_list.push_back(pinned);
    //     else
    //         h.visited[pinned] = true;
    // }
    // for(auto pinned : rm_list){
    //     pinned_insts.erase(pinned);
    // }
    for(auto pinned : pinned_insts){
        if(pinned->hasNoDef())
            continue;
        const auto& uses_list = pinned->getDef()->getUses();
        for(const auto use_inst : uses_list){
            schedule_late(use_inst);
        }
    }

    // move
    h.clear_visited(func);
    for(auto pinned : pinned_insts){
        h.visited[pinned] = true;
    }
    for(auto bb_it = func->begin();bb_it!=func->end();bb_it++){
        BasicBlock* bb = *bb_it;
        for(auto inst = bb->begin();inst!=bb->end();inst=inst->getNext()){
            if(h.is_pinned(inst)){
                for(const auto input : inst->getUses()){
                    if(input->getEntry()->isConstant() || input->getEntry()->isVariable()) 
                        continue;
                    Instruction* input_inst = input->getDef();
                    move(input_inst);
                }
            }
        }
    }

}

void GlobalCodeMotion::print_schedule()
{
    for(auto p : schedule_block){
        p.first->output();
        fprintf(stderr,"schedule block: %d\n",p.second->getNo());
    }
}

void Helper::post_order(BasicBlock *bb, std::stack<BasicBlock *> &s)
{
    if(bb_visited.count(bb)) return;
    bb_visited.insert(bb);
    for(auto succ_it = bb->succ_begin();succ_it!=bb->succ_end();succ_it++){
        BasicBlock* succ = *succ_it;
        post_order(succ,s);
    }
    s.push(bb);
}

void Helper::compute_info(Function *func)
{
    dom_depth.clear();
    loop_depth.clear();
    func->ComputeDom();
    // 计算支配树貌似有bug，有时候入口也会有支配者
    func->getEntry()->getIDom() = nullptr;
    std::unordered_map<BasicBlock*,std::vector<BasicBlock*>> dom_tree;
    for(auto bb_it = func->begin();bb_it!=func->end();bb_it++){
        BasicBlock* bb = *bb_it;
        if(bb->getIDom() == nullptr) continue;
        dom_tree[bb->getIDom()].push_back(bb);
    }
    std::queue<BasicBlock*> q;
    q.push(func->getEntry());
    dom_depth[func->getEntry()] = 0;
    while(!q.empty()){
        BasicBlock* bb = q.front();
        q.pop();
        for(auto child:dom_tree[bb]){
            dom_depth[child] = dom_depth[bb] + 1;
            q.push(child);
        }
    }

    //print dom tree
    for(auto p: dom_tree){
        fprintf(stderr,"bb%d:",p.first->getNo());
        for(auto child:p.second){
            fprintf(stderr,"bb%d ",child->getNo());
        }
        fprintf(stderr,"\n");
    }

    // 计算循环深度
    LoopAnalyzer la;
    // la.Analyze(func);
    la.FindLoops(func);
    for(auto bb_it = func->begin();bb_it!=func->end();bb_it++){
        BasicBlock* bb = *bb_it;
        loop_depth[bb] = 0;
    }
    for(auto loop:la.getLoops()){
        for(auto bb:loop->GetLoop()->GetBasicBlock()){
            loop_depth[bb] = std::max(loop->GetLoop()->GetDepth(),loop_depth[bb]);
        }
    }
    
    for(auto p : loop_depth){
        fprintf(stderr,"[gcm]bb%d:depth %d\n",p.first->getNo(),p.second);
    }

    // 计算插入点
    for(auto bb_it = func->begin();bb_it!=func->end();bb_it++){
        BasicBlock* bb = *bb_it;
        Instruction* i = bb->begin();
        while(i->isPHI())
            i = i->getNext();
        prepend_points[bb] = i;
        i=bb->end()->getPrev();
        while(i->isRet() || i->isCmp() || i->isCond() || i->isUncond())
            i = i->getPrev();
        append_points[bb] = i->getNext();
    }
}

void Helper::clear_visited(Function *func)
{
    visited.clear();
    for(auto bb_it = func->begin();bb_it!=func->end();bb_it++){
        BasicBlock* bb = *bb_it;
        for(auto inst = bb->begin();inst!=bb->end();inst=inst->getNext()){
            visited[inst] = false;
        }
    }    
}


BasicBlock *Helper::find_lca(BasicBlock *a, BasicBlock *b)
{
    if(a == nullptr) return b;
    while(get_dom_depth(a) > get_dom_depth(b))
        a = a->getIDom();
    while(get_dom_depth(b) > get_dom_depth(a))
        b = b->getIDom();
    while(a != b){
        a = a->getIDom();
        b = b->getIDom();
    }
    assert(a);
    return a;
}
