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
        default:
            break;
    }
    return opString;
}

void ValueNumbering::pass1(){
    for(auto it_func=unit->begin();it_func!=unit->end();it_func++){
        for(auto it_bb=(*it_func)->begin();it_bb!=(*it_func)->end();it_bb++){
            std::vector<Instruction*>to_remove;
            std::vector<Instruction*>new_mov;
            for(auto it_inst=(*it_bb)->begin();it_inst!=(*it_bb)->end();it_inst=it_inst->getNext()){
                
                std::string instStr=getOpString(it_inst);
                if(instStr=="") continue;

                for(auto use : it_inst->getUses()){
                    std::string operandStr=use->toStr();
                    if(!valueTable.count(operandStr))
                        valueTable[operandStr]=getValueNumber();
                }
                for(auto use : it_inst->getUses())
                    instStr+=std::to_string(valueTable[use->toStr()]);
                
                if(valueTable.count(instStr)){
                    //replace the inst with move
                    Operand* src=value2operand[valueTable[instStr]];
                    Operand* dst=it_inst->getDef();
                    Type* type= dst->getType()->isAnyInt() ? TypeSystem::constIntType : TypeSystem::constFloatType;
                    Instruction* mov = new BinaryInstruction(BinaryInstruction::ADD,dst,src,new Operand(new ConstantSymbolEntry(type,0)));
                    to_remove.push_back(it_inst);
                    new_mov.push_back(mov);
                    (*it_bb)->insertBefore(mov,it_inst);

                    std::string operandStr=dst->toStr();
                    valueTable[operandStr]=valueTable[instStr];
                }
                else{
                    int vnum=getValueNumber();
                    valueTable[instStr]=vnum;
                    valueTable[it_inst->getDef()->toStr()]=vnum;
                    value2operand[vnum]=it_inst->getDef();
                }
            }
            for(auto inst : to_remove){
                (*it_bb)->remove(inst);
                delete inst;
            }

            //eliminate redundant `mov`
            for(auto inst : new_mov){
                Operand* dst=inst->getDef();
                if(dst->getUses().size()==0){
                    (*it_bb)->remove(inst);
                    delete inst;
                }
            }
            valueTable.clear();
            value2operand.clear();
        }
    }
}

void ValueNumbering::lvn(BasicBlock* bb){
   // std::unordered_map<std::string,int>& valueTable=ctx.valueTable;
    //std::unordered_map<int,Operand*>& value2operand=ctx.value2operand;
    for(auto it_func=unit->begin();it_func!=unit->end();it_func++){
        for(auto it_bb=(*it_func)->begin();it_bb!=(*it_func)->end();it_bb++){
            std::vector<Instruction*>to_remove;
            std::vector<Instruction*>new_mov;
            for(auto it_inst=(*it_bb)->begin();it_inst!=(*it_bb)->end();it_inst=it_inst->getNext()){
                
                std::string instStr=getOpString(it_inst);
                if(instStr=="") continue;

                for(auto use : it_inst->getUses()){
                    std::string operandStr=use->toStr();
                    if(!valueTable.count(operandStr))
                        valueTable[operandStr]=getValueNumber();
                }
                for(auto use : it_inst->getUses())
                    instStr+=std::to_string(valueTable[use->toStr()]);
                
                if(valueTable.count(instStr)){
                    //replace the inst with move
                    Operand* src=value2operand[valueTable[instStr]];
                    Operand* dst=it_inst->getDef();
                    Type* type= dst->getType()->isAnyInt() ? TypeSystem::constIntType : TypeSystem::constFloatType;
                    Instruction* mov = new BinaryInstruction(BinaryInstruction::ADD,dst,src,new Operand(new ConstantSymbolEntry(type,0)));
                    to_remove.push_back(it_inst);
                    new_mov.push_back(mov);
                    (*it_bb)->insertBefore(mov,it_inst);

                    std::string operandStr=dst->toStr();
                    valueTable[operandStr]=valueTable[instStr];
                }
                else{
                    int vnum=getValueNumber();
                    valueTable[instStr]=vnum;
                    valueTable[it_inst->getDef()->toStr()]=vnum;
                    value2operand[vnum]=it_inst->getDef();
                }
            }
            for(auto inst : to_remove){
                (*it_bb)->remove(inst);
                delete inst;
            }

            //eliminate redundant `mov`
            for(auto inst : new_mov){
                Operand* dst=inst->getDef();
                if(dst->getUses().size()==0){
                    (*it_bb)->remove(inst);
                    delete inst;
                }
            }
            valueTable.clear();
            value2operand.clear();
        }
    }     
}

void ValueNumbering::svn(BasicBlock *bb)
{
    lvn(bb);
    for(auto it=bb->succ_begin();it!=bb->succ_end();it++){
        if(bb->getNumOfPred()==1)
            svn(*it);
        else
            worklist.push_back(*it);
    }    
}

void ValueNumbering::pass2(Function *func)
{
    std::set<BasicBlock *> v;
    BasicBlock* entry=func->getEntry();
    worklist.push_back(entry);
    while(!worklist.empty()){
        BasicBlock* bb=worklist.back();
        worklist.pop_back();
        valueTable.clear();
        value2operand.clear();
        svn(bb);
    }   
}

void ValueNumbering::dumpTable(){
    printf("key-num:\n");
    for(auto it=valueTable.begin();it!=valueTable.end();it++){
        printf("%s-%d\n",it->first.c_str(),it->second);
    }
    printf("num-operand:\n");
    for(auto it=value2operand.begin();it!=value2operand.end();it++){
        printf("%d-%s\n",it->first,it->second->toStr().c_str());
    }
}
