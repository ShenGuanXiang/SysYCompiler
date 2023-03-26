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
                exprs.insert(htable[instStr]);
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

                auto insts = def->getUses();
                for(auto i : insts){
                    if(i==curinst || !exprs.count(i->getDef())) continue;
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
    // for(auto func_it=unit->begin();func_it!=unit->end();func_it++){
    //     std::queue<BasicBlock*> q;
    //     std::set<BasicBlock*> visited;
    //     q.push((*func_it)->getEntry());
    //     visited.insert((*func_it)->getEntry());
    //     while(!q.empty()){
    //         auto curbb=q.front();
    //         q.pop();
    //         for(auto succ_it=curbb->succ_begin();succ_it!=curbb->succ_end();succ_it++){
    //             auto succ=*succ_it;
    //             //compute earliest
    //             Edge edge{curbb,succ};
    //             std::set<Operand*> diff,comple,uni,intersect;
    //             std::set_difference(antin[succ].begin(),antin[succ].end(),availout[curbb].begin(),availout[curbb].end(),std::inserter(diff,diff.end()));
    //             std::set_difference(allexpr[*func_it].begin(),allexpr[*func_it].end(),antout[curbb].begin(),antout[curbb].end(),std::inserter(comple,comple.end()));
    //             std::set_union(killexpr[curbb].begin(),killexpr[curbb].end(),comple.begin(),comple.end(),std::inserter(uni,uni.end()));
    //             std::set_intersection(diff.begin(),diff.end(),uni.begin(),uni.end(),std::inserter(intersect,intersect.end()));
    //             earliest[edge].insert(intersect.begin(),intersect.end());
    //             if(visited.find(succ)==visited.end()){
    //                 q.push(succ);
    //                 visited.insert(succ);
    //             }
    //         }
    //     }
    // }
}

void LazyCodeMotion::computeLater()
{
    // for(auto func_it=unit->begin();func_it!=unit->end();func_it++)
    // {
    //     auto dummy=new BasicBlock(*func_it);
    //     dummy->addSucc((*func_it)->getEntry());
    //     (*func_it)->getEntry()->addPred(dummy);

    //     for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++)
    //         availout[*bb_it].insert(allexpr[*func_it].begin(),allexpr[*func_it].end());
    //     availout[dummy].clear();

    //     bool changed=true;
    //     while(changed){
    //         changed=false;
    //         for(auto bb_it=(*func_it)->begin();bb_it!=(*func_it)->end();bb_it++){
    //             auto curbb=*bb_it;
    //             if(curbb==dummy) continue;
    //             std::set<Operand*> temp_laterin;
    //             for(auto pred_it=curbb->pred_begin();pred_it!=curbb->pred_end();pred_it++){
    //                 auto pred=*pred_it;
    //                 Edge e{pred,curbb};
    //                 if(temp_laterin.empty())
    //                     temp_laterin.insert(later[e].begin(),later[e].end());
    //                 else{
    //                     std::set<Operand*> intersect;
    //                     std::set_intersection(temp_laterin.begin(),temp_laterin.end(),later[e].begin(),later[e].end(),std::inserter(intersect,intersect.end()));
    //                     temp_laterin.insert(intersect.begin(),intersect.end());
    //                 }
    //                 if(temp_laterin!=laterin[curbb]){
    //                     laterin[curbb].clear();
    //                     laterin[curbb].insert(temp_laterin.begin(),temp_laterin.end());
    //                     changed=true;
    //                 }
    //             }


    //             // for(auto pred_it=curbb->pred_begin();pred_it!=curbb->pred_end();pred_it++){
    //             //     auto pred=*pred_it;
    //             //     Edge e{pred,curbb};
    //             //     later[e].clear();
    //             //     later[e].insert(earliest[e].begin(),earliest[e].end());
    //             //     std::set<Operand*> diff;
    //             //     std::set_difference(laterin[pred].begin(),)
    //             //     if(temp_laterin.empty())
    //             //         temp_laterin.insert(later[e].begin(),later[e].end());
    //             //     else{
    //             //         std::set<Operand*> intersect;
    //             //         std::set_intersection(temp_laterin.begin(),temp_laterin.end(),later[e].begin(),later[e].end(),std::inserter(intersect,intersect.end()));
    //             //         temp_laterin.insert(intersect.begin(),intersect.end());
    //             //     }
    //             //     if(temp_laterin!=laterin[curbb]){
    //             //         laterin[curbb].clear();
    //             //         laterin[curbb].insert(temp_laterin.begin(),temp_laterin.end());
    //             //         changed=true;
    //             //     }
    //             // }
    //         }
    //     }

    //     (*func_it)->getEntry()->removePred(dummy);
    //     dummy->removeSucc((*func_it)->getEntry());
    //     (*func_it)->remove(dummy);
    //     delete dummy;
    // }
    
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

void LazyCodeMotion::pass()
{
    collectAllexpr();
    computeLocal();
    computeAnt();
    computeEarliest();

}
