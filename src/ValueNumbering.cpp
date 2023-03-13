#include "ValueNumbering.h"
#include "Instruction.h"

#include<vector>
std::string ValueNumbering::getOpString(Instruction* inst){
    std::string opString = "";
    switch(inst->getInstType()){
        case Instruction::BINARY:
            switch (dynamic_cast<BinaryInstruction*>(inst)->getOpcode())
            {
            case BinaryInstruction::ADD:
                opString += "ADD";
                break;
            case BinaryInstruction::SUB:
                opString += "SUB";
                break;
            case BinaryInstruction::MUL:
                opString += "MUL";
                break;
            case BinaryInstruction::DIV:
                opString += "DIV";
                break;
            case BinaryInstruction::MOD:
                opString += "MOD";
                break;
            default:
                break;
            }
            break;
        case Instruction::GEP:
            opString +="GEP";
            break;
        case Instruction::PHI:
            opString +="PHI";
            break;
        default:
            break;
    }
    return opString;
}

void ValueNumbering::dumpTable(){
    printf("------\n");
    for(auto p=htable.begin();p!=htable.end();p++)
        printf("%s->%s\n",p->first.c_str(),p->second->toStr().c_str());
    printf("------\n");
}

void ValueNumbering::computeDomTree(Function* func)
{
    func->ComputeDom();
    for(auto it_bb=func->begin();it_bb!=func->end();it_bb++)
    {
        BasicBlock* bb=*it_bb;
        domtree[bb->getIDom()].push_back(bb);
    }
}

static void replaceWithinBB(Operand* src,Operand* dst,BasicBlock* bb)
{
    // replace dst with src in bb
    for (auto userInst : dst->getUses())
    {
        if(userInst->getParent()!=bb && userInst) continue;
        auto &uses = userInst->getUses();
        for (auto &use : uses)
            if (use == dst)
            {
                use->removeUse(userInst);
                use = src;
                src->addUse(userInst);
            }
    }
    
}

void ValueNumbering::dvnt(BasicBlock* bb)
{
    
    std::unordered_map<std::string,Operand*> m_htable; //current block's own local context
    //every time change htable, copy the new pair to m_table
    std::vector<Instruction*> torm; //instruction to remove
    for(auto cur_inst = bb->begin();cur_inst!=bb->end();cur_inst=cur_inst->getNext())
    {
        std::string instString;
        instString+=getOpString(cur_inst);
        if(instString=="") continue;
        if(instString=="PHI")
        {
            auto phi=dynamic_cast<PhiInstruction*>(cur_inst);
            Operand* dst=phi->getDef();
            auto args=phi->getSrcs();
            
            bool meanless=true;
            std::string preargstr;
            
            for(auto it_arg=args.begin();it_arg!=args.end();it_arg++){
                std::string argstr=",B"+std::to_string(it_arg->first->getNo())+it_arg->second->toStr();
                if(it_arg==args.begin()) preargstr=argstr;
                else meanless = (preargstr==argstr);
                instString+=argstr;
            }
            if(meanless){
                htable[dst->toStr()]=args.begin()->second;
                m_htable[dst->toStr()]=args.begin()->second;
                torm.push_back(cur_inst);
            }
            else if(m_htable.count(instString)){
                //redundant
                htable[dst->toStr()]=htable[instString];
                m_htable[dst->toStr()]=htable[instString];
                torm.push_back(cur_inst);
            }
            else{
                htable[dst->toStr()]=dst;
                htable[instString]=dst;
                m_htable[dst->toStr()]=dst;
                m_htable[instString]=dst;
            }
        }
        else
        {
            Operand* dst=cur_inst->getDef();

            for(auto use : cur_inst->getUses()){
                if(!htable.count(use->toStr())) m_htable[use->toStr()]=htable[use->toStr()]=use;
                instString+=","+htable[use->toStr()]->toStr();
            }
            if(htable.count(instString)){
                auto src=htable[instString];
                replaceWithinBB(src,dst,bb);
                torm.push_back(cur_inst);
                m_htable[dst->toStr()]=htable[dst->toStr()]=src;
            }
            else{
                m_htable[dst->toStr()]=htable[dst->toStr()]=dst;
                m_htable[instString]=htable[instString]=dst;
            }
        }
    }
    for(auto i :torm)
    {
        bb->remove(i);
        delete i;     
    }

    for(auto it_succ = bb->succ_begin();it_succ!=bb->succ_end();it_succ++){
        for(auto i = (*it_succ)->begin();i->getInstType()==Instruction::PHI;i=i->getNext()){
            auto phi=dynamic_cast<PhiInstruction*>(i);
            for (auto &src : phi->getSrcs())
            {
                auto& use=src.second;  //watch out: It is `reference` here !
                if(htable.count(use->toStr())){
                    src.second = htable[use->toStr()];
                    use->removeUse(i);
                    use = htable[use->toStr()];
                    htable[use->toStr()]->addUse(i);
                }
            }       
        }
    }

    for(auto child : domtree[bb]){
        dvnt(child);
    }


    //deallocate context for this basic block
    for(auto p = m_htable.begin();p!=m_htable.end();p++)
        htable.erase(p->first);
}

void ValueNumbering::pass3()
{
    for(auto it_func=unit->begin();it_func!=unit->end();it_func++)
    {
        auto entry=(*it_func)->getEntry();
        domtree.clear();
        computeDomTree(*it_func);
        dvnt(entry);
    }
}
