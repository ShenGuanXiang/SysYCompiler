#include "ValueNumbering.h"
#include "Instruction.h"

#include <vector>
std::string ValueNumbering::getOpString(Instruction *inst)
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
    case Instruction::GEP:
        instString += "GEP";
        break;
    case Instruction::PHI:
        instString += "PHI";
        break;
    case Instruction::IFCAST:
        switch (dynamic_cast<IntFloatCastInstruction*>(inst)->getOpcode())
        {
        case IntFloatCastInstruction::S2F:
            instString += "SF";
            break;
        case IntFloatCastInstruction::F2S:
            instString += "FS";
            break;
        default: assert(0);
        }
        break;
    default:
        break;
    }
    if(instString=="PHI")
    {
        auto phi = dynamic_cast<PhiInstruction *>(inst);
        auto args = phi->getSrcs();

        bool meaningless = true;
        Operand* prearg=phi->getUses()[0];

        for (auto it_arg = args.begin(); it_arg != args.end(); it_arg++)
        {
            //防止全局变量的名字对key进行hack
            std::string argstr = ",%B" + std::to_string(it_arg->first->getNo()) +","+ it_arg->second->toStr();
            if(prearg != it_arg->second)
                meaningless=false;
            else
                prearg=it_arg->second;
            instString += argstr;
        }
        if (meaningless){
            printf("\n");
            return "MEANINGLESS_PHI";
        }
    }
    else if(instString!=""){
        for(auto use : inst->getUses()){
        if(!htable.count(use->toStr())) 
            instString+=","+use->toStr();
        else
            instString+=htable[use->toStr()]->toStr();
        }
    }
    return instString;
}

void ValueNumbering::dumpTable()
{
    printf("------\n");
    for (auto p = htable.begin(); p != htable.end(); p++)
        printf("%s->%s\n", p->first.c_str(), p->second->toStr().c_str());
    printf("------\n");
}

void ValueNumbering::computeDomTree(Function *func)
{
    func->ComputeDom();
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        BasicBlock *bb = *it_bb;
        domtree[bb->getIDom()].push_back(bb);
    }
}

void ValueNumbering::dvnt(BasicBlock *bb)
{
    std::unordered_map<std::string, Operand *> prehtable; 
    prehtable = htable;// store curent htable, to restore after processing children
    std::vector<Instruction *> torm; // instruction to remove
    for (auto cur_inst = bb->begin(); cur_inst != bb->end(); cur_inst = cur_inst->getNext())
    {
        std::string instString;
        instString+=getOpString(cur_inst);
        if(instString=="") continue;
        Operand* dst=cur_inst->getDef();
        if(instString=="MEANINGLESS_PHI")
        {
            auto args = dynamic_cast<PhiInstruction *>(cur_inst)->getSrcs();
            htable[dst->toStr()] = args.begin()->second;
            torm.push_back(cur_inst);
            cur_inst->replaceAllUsesWith(args.begin()->second);
        }
        else if (instString.substr(0,3)=="PHI" &&
            htable.count(instString) &&
            htable[instString]->getDef()->getParent()==bb)
        {
            // redundant
            htable[dst->toStr()] = htable[instString];
            torm.push_back(cur_inst);
            cur_inst->replaceAllUsesWith(htable[instString]);
        }
        else
        {
            if(htable.count(instString)){
                auto src=htable[instString];
                torm.push_back(cur_inst);
                htable[dst->toStr()] = src;
                cur_inst->replaceAllUsesWith(src);
            }
            else
            {
                htable[instString] = dst;
            }
        }
    }
    for (auto i : torm)
    {
        bb->remove(i);
        delete i;
    }
    //倒着遍历支配树中的子节点，使得带有phi语句的块尽量靠后处理，尽可能消除phi语句
    //算法不保证消除所有不必要的phi语句，因为遍历phi语句时，其操作数可能没被更新
    for(auto it_child=domtree[bb].rbegin();it_child!=domtree[bb].rend();it_child++)
        dvnt(*it_child);

    // deallocate context for this basic block
    htable = prehtable;
}

void ValueNumbering::pass3()
{
    for (auto it_func = unit->begin(); it_func != unit->end(); it_func++)
    {
        auto entry = (*it_func)->getEntry();
        domtree.clear();
        computeDomTree(*it_func);
        dvnt(entry);
    }
}


//the following functions are used for value numbering in assmbly code generation phase


void ValueNumberingASM::computeDomTree(MachineFunction* func)
{
    func->computeDom();
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        MachineBlock *bb = *it_bb;
        domtree[bb->getIDom()].push_back(bb);
    }

    // print domtree
    // for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    // {
    //     MachineBlock *bb = *it_bb;
    //     printf("bb%d's dom children are:",bb->getNo());
    //     for(auto child:domtree[bb])
    //         printf("bb%d ",child->getNo());
    //     printf("\n");
    // }
}

std::string ValueNumberingASM::getOpString(MachineInstruction *minst)
{

    std::string instString = "";
    //忽略带有条件的指令，这种指令不能被消除，但是其操作数应该被替换
    if(minst->getCond() != MachineInstruction::NONE)
        return instString;

    switch (minst->getInstType())
    {
    case MachineInstruction::LOAD: // for load imm
        if((minst->getUse().size() == 1) && minst->getUse()[0]->isImm())
            instString += "LOADI";
        else if((minst->getUse().size() == 1) && minst->getUse()[0]->isLabel())
            instString += "LOADG";
        break;
    case MachineInstruction::BINARY:
        switch (dynamic_cast<BinaryMInstruction *>(minst)->getOpType())
        {
        case BinaryMInstruction::ADD:
            instString += "ADD";
            break;
        case BinaryMInstruction::SUB:
            instString += "SUB";
            break;
        case BinaryMInstruction::MUL:
            instString += "MUL";
            break;
        case BinaryMInstruction::DIV:
            instString += "DIV";
            break;
        case BinaryMInstruction::AND:
            instString += "AND";
            break;
        case BinaryMInstruction::RSB:
            instString += "RSB";
            break;
        default: assert(0);
        }
        break;
    case MachineInstruction::MOV:
        switch (dynamic_cast<MovMInstruction*>(minst)->getOpType())
        {
        case MovMInstruction::MOV:
            instString="MOV";
            break;
        case MovMInstruction::VMOV:
            instString="VMOV";
            break;
        case MovMInstruction::MOVLSL:
            instString="MOVLSL";
            break;
        case MovMInstruction::MOVLSR:
            instString="MOVLSR";
            break;
        case MovMInstruction::MOVASR:
            instString="MOVASR";
            break;
        default:assert(0);
        }
        break;
    case MachineInstruction::VCVT:
        instString += "VCVT";
        break;
    default:
        break;
    }
    return instString;
}

void ValueNumberingASM::pass()
{
    for (auto it_mfunc = munit->begin(); it_mfunc != munit->end(); it_mfunc++)
    {
        auto entry = (*it_mfunc)->getEntry();
        domtree.clear();
        computeDomTree(*it_mfunc);
        r0redef=false;
        findredef(entry);
        rhtable.clear();
        dvnt(entry);
    }
    rollbackrpl();
    for(auto i : torm){
        auto mbb=i->getParent();
        mbb->remove(i);
    }
}
void ValueNumberingASM::dvnt(MachineBlock* bb)
{
    std::unordered_map<std::string, MachineOperand *> prehtable; 
    prehtable = htable;// store curent htable, to restore after processing children
    for(auto it_minst=bb->begin();it_minst!=bb->end();it_minst++)
    {
        
        auto inst=*it_minst;

        for(auto& use : inst->getUse())
            if(htable.count(use->toStr())){
                auto mop=new MachineOperand(*htable[use->toStr()]);
                addrpl(use,mop,&use);
                use=mop;
                use->setParent(inst);
            }
            else if(rhtable.count(use->toStr())){
                auto mop=new MachineOperand(*rhtable[use->toStr()]);
                addrpl(use,mop,&use);
                use=mop;
                use->setParent(inst);
            }
                
                
        std::string instString = getOpString(inst);
        if(instString=="") continue;
        if(inst->getDef()[0]->isReg()) continue;
        int withreg=-1;
        for(auto use : inst->getUse()){
            auto usestr=use->toStr();
            if(htable.count(usestr))
                instString+=","+htable[usestr]->toStr();
            else
                instString+=","+usestr;
            if(withreg==-1 && use->isReg())
                withreg=use->getReg();
        }
        if(withreg!=-1 && withreg==0) continue;
        
        //only fp inst could be removed

        auto dst=inst->getDef()[0];
        if(redef.count(*dst)) continue;

        
        if(withreg!=-1){
            if(rhtable.count(instString)){
                auto src = rhtable[instString];
                rhtable[dst->toStr()]=src;
                rpltable[*dst].inst=inst;
                torm.push_back(inst);
            }
            else 
                rhtable[instString]=dst;
        }
        else{
            if(htable.count(instString)){
                auto src=htable[instString];
                htable[dst->toStr()]=src;
                rpltable[*dst].inst=inst;
                torm.push_back(inst);
            }
            else 
                htable[instString]=dst;
        }
    }
    for(auto mb : domtree[bb])
        dvnt(mb);
        
    htable=prehtable;
}

void ValueNumberingASM::rollbackrpl()
{
    for(auto it=rpltable.begin();it!=rpltable.end();it++){
        auto origin_mop = it->first;
        auto definst=it->second.inst;
        auto& rpls=it->second.rpl;
        if(usescnt[origin_mop]==0 || rpls.empty()) continue;

        for(auto it_rpl=rpls.begin();it_rpl!=rpls.end();it_rpl++)
            *(it_rpl->first)=it_rpl->second;
        auto bb=definst->getParent();
        MachineOperand* src = new MachineOperand(*rpls[0].second);
        MachineOperand* dst=new MachineOperand(*definst->getDef()[0]);
        MachineInstruction* mov;
        if(dst->getValType()->isFloat())
            mov = new MovMInstruction(bb,MovMInstruction::VMOV, dst, src);
        else
            mov = new MovMInstruction(bb,MovMInstruction::MOV, dst, src);
        bb->insertAfter(definst,mov);
    }
        
}

void ValueNumberingASM::dumpTable(){
    printf("------\n");
    for(auto it=htable.begin();it!=htable.end();it++)
        std::cout<<it->first<<" "<<it->second->toStr()<<std::endl;
    printf("------\n");
}
void ValueNumberingASM::addrpl(MachineOperand *from, MachineOperand *to, MachineOperand **place)
{
    assert(rpltable.count(*from));
    rpltable[*from].rpl.push_back({place, to});
    usescnt[*from]--;
}
void ValueNumberingASM::findredef(MachineBlock *bb)
{
    std::set<MachineOperand> curset;
    std::copy(defset.begin(),defset.end(),std::inserter(curset,curset.end()));
    for(auto it_minst=bb->begin();it_minst!=bb->end();it_minst++)
    {
        if((*it_minst)->isBranch() && (*it_minst)->getOpType()==::BranchMInstruction::BL)
            r0redef=true;
        
        for(auto use : (*it_minst)->getUse()){
            if(usescnt.count(*use))
                usescnt[*use]++;
            else usescnt[*use]=1;
        }

        auto inst=*it_minst;
        if(inst->getDef().size()==0) continue;
        
        auto def=inst->getDef()[0];
       // if(def->isReg()) continue;
        if(defset.count(*def))
            redef.insert(*def);
        else defset.insert(*def);
    }
    for(auto mb : domtree[bb])
        findredef(mb);
    defset.clear();
    std::copy(curset.begin(),curset.end(),std::inserter(defset,defset.end()));
}