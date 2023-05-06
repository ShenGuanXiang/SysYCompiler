#include "LazyCodeMotion.h"

#include <vector>
#include <queue>

//写了好几个这种函数，后面考虑简化一下
std::string LazyCodeMotion::getOpString(Instruction *inst)
{
    std::string instString = "";
    switch (inst->getInstType())
    {
    case Instruction::BINARY:
        switch (dynamic_cast<BinaryInstruction *>(inst)->getOpcode())
        {
        case BinaryInstruction::ADD:
            instString += "ADD";
            break;
        case BinaryInstruction::SUB:
            instString += "SUB";
            break;
        case BinaryInstruction::MUL:
            instString += "MUL";
            break;
        case BinaryInstruction::DIV:
            instString += "DIV";
            break;
        case BinaryInstruction::MOD:
            instString += "MOD";
            break;
        default: assert(0);
        }
        break;
    default:
        break;
    }

    if(instString!=""){
        for(auto use : inst->getUses()){
        if(!htable.count(use->toStr())) 
            instString+=","+use->toStr();
        else
            instString+=htable[use->toStr()]->toStr();
        }
    }
    return instString;
}

void LazyCodeMotion::collectAllexpr()
{
    for(auto func_it=unit->begin();func_it!=unit->end();func_it++)
    {
        auto curfunc=*func_it;
        allexpr[curfunc].clear();
        std::set<Operand*>& exprs=allexpr[*func_it];
        for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++)
        {
            for(auto curinst=(*bb_it)->begin();curinst!=(*bb_it)->end();curinst=curinst->getNext())
            {
                auto instStr=getOpString(curinst);
                if(instStr=="") continue;
                assert(htable.count(instStr));
                Operand* vnr=htable[instStr];
                exprs.insert(vnr);
                if(vnr!=curinst->getDef())
                    exprmap[*bb_it][vnr]=curinst->getDef();
            }
        }
    }
}

void LazyCodeMotion::computeLocal()
{
    for(auto func_it=unit->begin();func_it!=unit->end();func_it++)
    {
        std::set<Operand*>& exprs = allexpr[*func_it];
        for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++)
        {
            auto curbb=*bb_it;
            for(auto curinst=(*bb_it)->begin();curinst!=(*bb_it)->end();curinst=curinst->getNext())
            {
                auto instStr=getOpString(curinst);
                if(!htable.count(instStr)) continue;
                auto def=curinst->getDef();
                if(htable.count(def->toStr())) def=htable[def->toStr()];

                bool isue=true; //is upward exposed, i.e. not arg is defined in this bb
                for(auto use : curinst->getUses()){
                    if(!exprs.count(use)) continue;
                    if(use->getDef()->getParent()==curbb){
                        isue=false;
                        break;
                    }
                }
                if(isue)
                    ueexpr[curbb].insert(def);

                deexpr[curbb].insert(def);

                // auto insts = def->getUses();
                // for(auto i : insts){
                //     if(i==curinst || !exprs.count(i->getDef())) continue;
                //     killexpr[curbb].insert(i->getDef());
                // }
            }

            for(auto curinst=(*bb_it)->begin();curinst!=(*bb_it)->end();curinst=curinst->getNext())
            {
                if(!curinst->hasDst()) continue;
                auto def=curinst->getDef();
                auto insts = def->getUses();
                for(auto i : insts){
                    if(!i->hasDst()) continue;
                    if(i==curinst  ||!exprs.count(i->getDef())) continue;
                    killexpr[curbb].insert(i->getDef());
                }
            }
        }
    }
}

void LazyCodeMotion::computeAvail()
{
    for(auto func_it=unit->begin();func_it!=unit->end();func_it++)
    {
        auto dummy=new BasicBlock(*func_it);
        dummy->addSucc((*func_it)->getEntry());
        (*func_it)->getEntry()->addPred(dummy);

        for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++)
            availout[*bb_it].insert(allexpr[*func_it].begin(),allexpr[*func_it].end());
        availout[dummy].clear();

        bool changed=true;
        while(changed){
            changed=false;
            for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++){
                auto curbb=*bb_it;
                std::set<Operand*> temp_availout;
                for(auto pred_it=curbb->pred_begin();pred_it!=curbb->pred_end();pred_it++){
                    auto pred=*pred_it;
                    std::set<Operand*> diff;
                    std::set_difference(availout[pred].begin(),availout[pred].end(),killexpr[pred].begin(),killexpr[pred].end(),std::inserter(diff,diff.end()));
                    std::set<Operand*> tmp;
                    std::set_union(diff.begin(),diff.end(),deexpr[curbb].begin(),deexpr[curbb].end(),std::inserter(tmp,tmp.end()));
                    if(temp_availout.empty())
                        temp_availout.insert(tmp.begin(),tmp.end());
                    else{
                        std::set<Operand*> intersect;
                        std::set_intersection(temp_availout.begin(),temp_availout.end(),tmp.begin(),tmp.end(),std::inserter(intersect,intersect.end()));
                        temp_availout.clear();
                        temp_availout.insert(intersect.begin(),intersect.end());
                    }
                }
                if(temp_availout!=availout[curbb]){
                    availout[curbb].clear();
                    availout[curbb].insert(temp_availout.begin(),temp_availout.end());
                    changed=true;
                }
            }

        }

        (*func_it)->getEntry()->removePred(dummy);
        dummy->removeSucc((*func_it)->getEntry());
        (*func_it)->remove(dummy);
        delete dummy;
    }
    
}

void LazyCodeMotion::computeAnt()
{
    for(auto func_it=unit->begin();func_it!=unit->end();func_it++)
    {
        //add dummy exit point
        auto dummy=new BasicBlock(*func_it);
        for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++){
            auto bb=*bb_it;
            if(bb->getNumOfSucc()!=0 || bb==dummy) continue;
            dummy->addPred(bb);
            bb->addSucc(dummy);  
        }
        //initialize 
        for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++){
            antout[*bb_it].insert(allexpr[*func_it].begin(),allexpr[*func_it].end());
        }
        antout[dummy].clear();

        //iteratively solve
        bool changed=true;
        while(changed){
            changed=false;

            for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++){
                auto curbb=*bb_it;
                std::set<Operand*> temp_diff;
                std::set_difference(antout[curbb].begin(),antout[curbb].end(),killexpr[curbb].begin(),killexpr[curbb].end(),std::inserter(temp_diff,temp_diff.end()));
                
                std::set<Operand*> temp_antin;
                std::set_union(ueexpr[curbb].begin(),ueexpr[curbb].end(),temp_diff.begin(),temp_diff.end(),std::inserter(temp_antin,temp_antin.end()));
                if(temp_antin!=antin[curbb]){
                    antin[curbb].clear();
                    antin[curbb].insert(temp_antin.begin(),temp_antin.end());
                    changed=true;
                }

                if(curbb==dummy) continue;

                std::set<Operand*> temp_antout;                
                for(auto succ_it=curbb->succ_begin();succ_it!=curbb->succ_end();succ_it++){
                    auto cur_succ=*succ_it;
                    if(succ_it==curbb->succ_begin())
                        temp_antout.insert(antin[cur_succ].begin(),antin[cur_succ].end());
                    else{
                        std::set<Operand*> intersect;
                        std::set_intersection(temp_antout.begin(),temp_antout.end(),antin[cur_succ].begin(),antin[cur_succ].end(),std::inserter(intersect,intersect.end()));
                        temp_antout.clear();
                        temp_antout.insert(intersect.begin(),intersect.end());
                    }
                }

                if(temp_antout!=antout[curbb]){
                    antout[curbb].clear();
                    antout[curbb].insert(temp_antout.begin(),temp_antout.end());
                    changed=true;
                }


            }

        }

        //remove dummy exit point
        for(auto pred_it=dummy->pred_begin();pred_it!=dummy->pred_end();pred_it++){
            auto pred=*pred_it;
            pred->removeSucc(dummy);
        }
        (*func_it)->remove(dummy);
        delete dummy;
    }
    
}

void LazyCodeMotion::computeEarliest()
{
    //bfs the cfg
    for(auto func_it=unit->begin();func_it!=unit->end();func_it++){
        std::queue<BasicBlock*> q;
        std::set<BasicBlock*> visited;
        q.push((*func_it)->getEntry());
        visited.insert((*func_it)->getEntry());
        while(!q.empty()){
            auto curbb=q.front();
            q.pop();
            for(auto succ_it=curbb->succ_begin();succ_it!=curbb->succ_end();succ_it++){
                auto succ=*succ_it;
                //compute earliest
                Edge edge{curbb,succ};
                std::set<Operand*> diff,comple,uni,intersect;
                std::set_difference(antin[succ].begin(),antin[succ].end(),availout[curbb].begin(),availout[curbb].end(),std::inserter(diff,diff.end()));
                std::set_difference(allexpr[*func_it].begin(),allexpr[*func_it].end(),antout[curbb].begin(),antout[curbb].end(),std::inserter(comple,comple.end()));
                std::set_union(killexpr[curbb].begin(),killexpr[curbb].end(),comple.begin(),comple.end(),std::inserter(uni,uni.end()));
                std::set_intersection(diff.begin(),diff.end(),uni.begin(),uni.end(),std::inserter(intersect,intersect.end()));
                earliest[edge].insert(intersect.begin(),intersect.end());
                if(visited.find(succ)==visited.end()){
                    q.push(succ);
                    visited.insert(succ);
                }
            }
        }
    }
}

void LazyCodeMotion::computeLater()
{
    for(auto func_it=unit->begin();func_it!=unit->end();func_it++)
    {
        auto dummy=new BasicBlock(*func_it);
        dummy->addSucc((*func_it)->getEntry());
        (*func_it)->getEntry()->addPred(dummy);

        for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++)
            laterin[*bb_it].insert(allexpr[*func_it].begin(),allexpr[*func_it].end());
        laterin[dummy].clear();

        bool changed=true;
        while(changed){
            changed=false;
            for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++){
                auto curbb=*bb_it;
                if(curbb==dummy) continue;
                std::set<Operand*> temp_laterin;
                for(auto pred_it=curbb->pred_begin();pred_it!=curbb->pred_end();pred_it++){
                    auto pred=*pred_it;
                    Edge e{pred,curbb};
                    //print edge
                    std::cout<<"edge: "<<e.src->getNo()<<"->"<<e.dst->getNo()<<std::endl;

                    if(pred_it==curbb->pred_begin())
                        temp_laterin.insert(later[e].begin(),later[e].end());
                    else{
                        std::set<Operand*> intersect;
                        std::set_intersection(temp_laterin.begin(),temp_laterin.end(),later[e].begin(),later[e].end(),std::inserter(intersect,intersect.end()));
                        temp_laterin.clear();
                        temp_laterin.insert(intersect.begin(),intersect.end());
                    }
                }

                if(temp_laterin!=laterin[curbb]){
                    laterin[curbb].clear();
                    laterin[curbb].insert(temp_laterin.begin(),temp_laterin.end());
                    changed=true;
                }


                for(auto pred_it=curbb->pred_begin();pred_it!=curbb->pred_end();pred_it++){
                    auto pred=*pred_it;
                    Edge e{pred,curbb};
                    std::set<Operand*> diff,temp_later,uni;
                    std::set_difference(laterin[pred].begin(),laterin[pred].end(),ueexpr[pred].begin(),ueexpr[pred].end(),std::inserter(diff,diff.end()));
                    std::set_union(earliest[e].begin(),earliest[e].end(),diff.begin(),diff.end(),std::inserter(temp_later,temp_later.end()));
                    if(temp_later!=later[e]){
                        later[e].clear();
                        later[e].insert(temp_later.begin(),temp_later.end());
                        changed=true;
                    }
                }


            }
        }

        (*func_it)->getEntry()->removePred(dummy);
        dummy->removeSucc((*func_it)->getEntry());
        (*func_it)->remove(dummy);
        delete dummy;
    }
    
}

void LazyCodeMotion::rewrite()
{
    //bfs
    for(auto func_it=unit->begin();func_it!=unit->end();func_it++){
        std::queue<BasicBlock*> q;
        std::set<BasicBlock*> visited;
        q.push((*func_it)->getEntry());
        visited.insert((*func_it)->getEntry());
        while(!q.empty()){
            auto curbb=q.front();
            q.pop();
            deleteset[curbb].clear();
            std::set_difference(ueexpr[curbb].begin(),ueexpr[curbb].end(),laterin[curbb].begin(),laterin[curbb].end(),std::inserter(deleteset[curbb],deleteset[curbb].end()));
            
            for(auto succ_it=curbb->succ_begin();succ_it!=curbb->succ_end();succ_it++){
                auto succ=*succ_it;

                Edge e{curbb,succ};
                insertset[e].clear();
                std::set_difference(later[e].begin(),later[e].end(),laterin[succ].begin(),laterin[succ].end(),std::inserter(insertset[e],insertset[e].end()));           
                
                if(visited.find(succ)==visited.end()){
                    q.push(succ);
                    visited.insert(succ);
                }
            }
        }
    }
    //print delete set, insert set
    std::cout<<"delete set"<<std::endl;
    for(auto it=deleteset.begin();it!=deleteset.end();it++){
        std::cout<<it->first->getNo()<<": ";
        for(auto def : it->second){
            std::cout<<def->toStr()<<" ";
        }
        std::cout<<std::endl;
    }
    std::cout<<"insert set"<<std::endl;
    for(auto it=insertset.begin();it!=insertset.end();it++){
        auto e=it->first;
        //print e
        std::cout<<e.src->getNo()<<"->"<<e.dst->getNo()<<": ";
        for(auto def : it->second){
            std::cout<<def->toStr()<<" ";
        }
        std::cout<<std::endl;
    }
    //rewirte
    for(auto it=insertset.begin();it!=insertset.end();it++){
        if(it->second.empty()) continue;
        BasicBlock* src=it->first.src;
        BasicBlock* dst=it->first.dst;
        std::set<Operand*>& exprset=it->second;
        if(src->getNumOfSucc()==1){
            for(auto def : exprset){
                auto inst = cloneExpr(def->getDef());
                //llvm ir has only one br instruction at the end of basic block
                src->insertBefore(inst,dst->rbegin());
            }
        }
        else if(dst->getNumOfPred()==1){
            for(auto def : exprset){
                auto inst = cloneExpr(def->getDef());
               
                Instruction* i=dst->begin();
                while(i->getInstType()==Instruction::PHI) i=i->getNext();
                dst->insertBefore(i,inst);
            }
        }
        else{
            
            BasicBlock* bb = new BasicBlock(src->getParent());
            //insert expression and br
            for(auto def : exprset){
                auto inst = cloneExpr(def->getDef());
                bb->insertBack(inst);
                inst->setParent(bb);
            }
            new UncondBrInstruction(dst,bb);
            

            //refactor cfg
            src->addSucc(bb);
            bb->addSucc(dst);
            bb->addPred(src);
            dst->addPred(bb);
            src->removeSucc(dst);
            dst->removePred(src);

            //refactor br
            Instruction* srcbr=src->rbegin();
            if(srcbr->getInstType()==Instruction::COND){
                CondBrInstruction* condbr = dynamic_cast<CondBrInstruction*>(srcbr);
                if(condbr->getTrueBranch()==dst)
                    condbr->setTrueBranch(bb);
                else condbr->setFalseBranch(bb);
            }
            else
                dynamic_cast<UncondBrInstruction*>(srcbr)->setBranch(bb);

            //refactor phi
            for(auto inst=dst->begin();inst!=dst->end();inst=inst->getNext()){
                if(inst->getInstType()!=Instruction::PHI) break;
                PhiInstruction* phi = dynamic_cast<PhiInstruction*>(inst);
                auto& phiargs = phi->getSrcs();
                for(auto arg : phiargs){
                    if(arg.first==src){
                        phiargs.insert({bb,arg.second});
                        phiargs.erase(arg.first);
                        break;
                    }
                }
            }
            
        }
            
    }

    for(auto it=deleteset.begin();it!=deleteset.end();it++){
        if(it->second.empty()) continue;
        auto bb=it->first;
        auto& exprset=it->second;
        for(auto def : exprset){
            //TODO：直接通过def找到实际的指令，这里的def和实际的操作数不相同
            Instruction* vnrdef = def->getDef();
            if(vnrdef->getParent()==bb)
                bb->remove(vnrdef);
            else{
                assert(exprmap[bb].count(def));
                Instruction* expr = exprmap[bb][def]->getDef();
                expr->replaceAllUsesWith(def);
                bb->remove(expr);
            }
        }
    }
}

void LazyCodeMotion::printAnt(){
    //print antin/antout
    for(auto bb_it=antin.begin();bb_it!=antin.end();bb_it++){
        auto curbb=bb_it->first;
        std::cout<<"antin of bb"<<curbb->getNo()<<": ";
        for(auto expr : antin[curbb]){
            std::cout<<expr->toStr()<<" ";
        }
        std::cout<<std::endl;
        std::cout<<"antout of bb"<<curbb->getNo()<<": ";
        for(auto expr : antout[curbb]){
            std::cout<<expr->toStr()<<" ";
        }
        std::cout<<std::endl;
    }
}

void LazyCodeMotion::printLoal()
{
    //print local: deexpr, ueexpr, killexpr
    auto func=(*unit->begin());
    for(auto bb_it=func->begin();bb_it!=func->end();bb_it++){
        auto curbb=*bb_it;
        std::cout<<"deexpr of bb"<<curbb->getNo()<<": ";
        for(auto expr : deexpr[curbb]){
            std::cout<<expr->toStr()<<" ";
        }
        std::cout<<std::endl;
        std::cout<<"ueexpr of bb"<<curbb->getNo()<<": ";
        for(auto expr : ueexpr[curbb]){
            std::cout<<expr->toStr()<<" ";
        }
        std::cout<<std::endl;
        std::cout<<"killexpr of bb"<<curbb->getNo()<<": ";
        for(auto expr : killexpr[curbb]){
            std::cout<<expr->toStr()<<" ";
        }
        std::cout<<std::endl;
    }
}

void LazyCodeMotion::printall()
{
    //print allexpr
    for(auto it=allexpr.begin();it!=allexpr.end();it++){
        for(auto expr : it->second){
            std::cout<<expr->toStr()<<" ";
        }
        std::cout<<std::endl;
    }

}

void LazyCodeMotion::printLater()
{
    //print laterin/later
    for(auto bb_it=laterin.begin();bb_it!=laterin.end();bb_it++){
        auto curbb=bb_it->first;
        std::cout<<"laterin of bb"<<curbb->getNo()<<": ";
        for(auto expr : laterin[curbb]){
            std::cout<<expr->toStr()<<" ";
        }
        std::cout<<std::endl;
    }
    for(auto it=later.begin();it!=later.end();it++){
        auto src=it->first.src;
        auto dst=it->first.dst;
        std::cout<<"later of edge ("<<src->getNo()<<","<<dst->getNo()<<"): ";
        for(auto expr : later[it->first]){
            std::cout<<expr->toStr()<<" ";
        }
        std::cout<<std::endl;
    }
}

void LazyCodeMotion::pass()
{
    collectAllexpr();
    computeLocal();
    computeAvail();
    computeAnt();
    computeEarliest();
    //print earliest
    for(auto it=earliest.begin();it!=earliest.end();it++){
        auto src=it->first.src;
        auto dst=it->first.dst;
        std::cout<<"earliest of edge ("<<src->getNo()<<","<<dst->getNo()<<"): ";
        for(auto expr : earliest[it->first]){
            std::cout<<expr->toStr()<<" ";
        }
        std::cout<<std::endl;
    }
    computeLater();
    rewrite();
}

Instruction* LazyCodeMotion::cloneExpr(Instruction* inst){
    //clone expr of Instruction* type from inst, according to getInstType()
    Instruction* newinst;
    switch(inst->getInstType()){
        case Instruction::BINARY :
            newinst = new BinaryInstruction(*dynamic_cast<BinaryInstruction*>(inst));
            break;
        default:
            assert(0);
    }
    newinst->setNext(nullptr);
    newinst->setPrev(nullptr);
//    newinst->getParent()->remove(newinst);
    newinst->setParent(nullptr);
    return newinst;
}