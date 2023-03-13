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

void ValueNumbering::pass1(){
    for(auto it_func=unit->begin();it_func!=unit->end();it_func++){
        for(auto it_bb=(*it_func)->begin();it_bb!=(*it_func)->end();it_bb++){
            std::vector<Instruction*>to_remove;
            for(auto it_inst=(*it_bb)->begin();it_inst!=(*it_bb)->end();it_inst=it_inst->getNext()){
                
                std::string instStr=getOpString(it_inst);
                if(instStr=="") continue;

                for(auto use : it_inst->getUses()){
                    std::string operandStr=use->toStr();
                    if(!valueTable.count(operandStr))
                        htable[operandStr]=use;
                        //valueTable[operandStr]=getValueNumber();
                }
                for(auto use : it_inst->getUses())
                    instStr+=","+use->toStr();
                    //instStr+=std::to_string(valueTable[use->toStr()]);

                if(htable.count(instStr))
                {
                    Operand* src=htable[instStr];
                    Operand* dst=it_inst->getDef();
                    //only replace within this bb
                    for (auto userInst : dst->getUses())
                    {
                        if(userInst->getParent()!=*it_bb) continue;
                        auto &uses = userInst->getUses();
                        for (auto &use : uses)
                            if (use ==dst)
                            {
                                if (userInst->isPHI())
                                {
                                    auto &args = ((PhiInstruction *)userInst)->getSrcs();
                                    for (auto &arg : args)
                                        if (arg.second == use)
                                            arg.second = src;
                                }
                                use->removeUse(userInst);
                                use = src;
                                src->addUse(userInst);
                            }
                    }

                    htable[dst->toStr()]=htable[instStr];
                    to_remove.push_back(it_inst);
                }
                else
                {
                    Operand* dst=it_inst->getDef();
                    htable[instStr]=dst;
                    htable[dst->toStr()]=dst;
                }
            }
            dumpTable();
            for(auto i : to_remove)
                (*it_bb)->remove(i);
            htable.clear();
        }
    }
}

void ValueNumbering::lvn(BasicBlock* bb) // a wrapper of `pass1`, used in pass2
{
    std::vector<Instruction*>to_remove;
        for(auto it_inst=bb->begin();it_inst!=bb->end();it_inst=it_inst->getNext()){
            
            std::string instStr=getOpString(it_inst);
            if(instStr=="") continue;

            for(auto use : it_inst->getUses()){
                std::string operandStr=use->toStr();
                if(!valueTable.count(operandStr))
                    htable[operandStr]=use;
            }
            for(auto use : it_inst->getUses())
                instStr+=","+use->toStr();

            if(htable.count(instStr))
            {
                Operand* src=htable[instStr];
                Operand* dst=it_inst->getDef();
                //only replace within this bb
                for (auto userInst : dst->getUses())
                {
                    if(userInst->getParent()!=bb) continue;
                    auto &uses = userInst->getUses();
                    for (auto &use : uses)
                        if (use ==dst)
                        {
                            if (userInst->isPHI())
                            {
                                auto &args = ((PhiInstruction *)userInst)->getSrcs();
                                for (auto &arg : args)
                                    if (arg.second == use)
                                        arg.second = src;
                            }
                            use->removeUse(userInst);
                            use = src;
                            src->addUse(userInst);
                        }
                }

                htable[dst->toStr()]=htable[instStr];
                to_remove.push_back(it_inst);
            }
            else
            {
                Operand* dst=it_inst->getDef();
                htable[instStr]=dst;
                htable[dst->toStr()]=dst;
            }
        }
        dumpTable();
        for(auto i : to_remove)
            bb->remove(i);
        htable.clear();
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

static void replaceWithinBB(Operand* src,Operand* dst,BasicBlock* bb,bool isgep=false)
{
    if(isgep){
        for(auto i=bb->begin();i!=bb->end();i=i->getNext()){
            if(i->getInstType()==Instruction::LOAD || i->getInstType()==Instruction::STORE || i->getInstType()==Instruction::CALL || i->getInstType()==Instruction::GEP){
                for(auto& use : i->getUses()){
                    if(use->toStr()==dst->toStr()){
                        use->removeUse(i);
                        use=src;
                        src->addUse(i);
                    }
                }
            }
        }
        return;
    }
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
                std::string argstr="B"+std::to_string(it_arg->first->getNo())+it_arg->second->toStr();
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
                instString+=htable[use->toStr()]->toStr();
            }
            if(htable.count(instString)){
                auto src=htable[instString];
                replaceWithinBB(src,dst,bb,cur_inst->getInstType()==Instruction::GEP);
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
    std::unordered_map<std::string,Operand*>res;
    std::set_difference(htable.begin(), htable.end(), m_htable.begin(), m_htable.end(),
                    std::inserter(res, res.end()));
    htable.swap(res);
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
