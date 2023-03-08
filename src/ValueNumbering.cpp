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
        // case Instruction::LOAD:
        //     opString += "load";
        //     break;
        // case Instruction::STORE:
        //     opString += "store";
        //     break;
        case Instruction::ZEXT:
            opString += "zext";
            break;
        // case Instruction::GEP:
        //     opString += "gep";
        //     break;
        default:
            break;
    }
    return opString;
}

void ValueNumbering::pass(){
    for(auto it_func=unit->begin();it_func!=unit->end();it_func++){
        for(auto it_bb=(*it_func)->begin();it_bb!=(*it_func)->end();it_bb++){
            //local value numbering with an basic block
            for(auto it_inst=(*it_bb)->begin();it_inst!=(*it_bb)->end();it_inst=it_inst->getNext()){
                std::string opString = getOpString(it_inst);
                if(opString=="")
                    continue;
                std::string key = opString;
                for(auto use : it_inst->getUses()){
                    std::string opstr=use->toStr();
                    if(valueTable.find(opstr)==valueTable.end()){
                        unsigned num= getValueNumber();
                        valueTable[opstr] = num;
                        value2operand[num] = use;
                    }
                    key += std::to_string(valueTable[opstr]);
                }
                if(valueTable.find(key)!=valueTable.end()){
                    Operand* dst=it_inst->getDef();
                    Type* dst_type= dst->getType()->isAnyInt() ? TypeSystem::constIntType 
                                            : TypeSystem::constFloatType;
                    unsigned vnum=valueTable[key];
                    Instruction* inst = new BinaryInstruction(BinaryInstruction::ADD,
                        dst,new Operand(new ConstantSymbolEntry(dst_type,0)),
                        new Operand(*value2operand[vnum]));
                    (*it_bb)->insertBefore(inst,it_inst);
                    inst->setNext(it_inst->getNext());
                   // delete it_inst; 
                   //Didn't delete the instruction, memory leak risk
                }
                valueTable[key] = getValueNumber();
                value2operand[valueTable[key]] = it_inst->getDef();
                valueTable[it_inst->getDef()->toStr()] = valueTable[key];
            }
            valueTable.clear();
            value2operand.clear();
        }

    }
    //dumpTable();
}

void ValueNumbering::lvn(BasicBlock* bb){
    for(auto it_inst=bb->begin();it_inst!=bb->end();it_inst=it_inst->getNext()){
        std::string opString = getOpString(it_inst);
        if(opString=="")
            continue;
        std::string key = opString;
        for(auto use : it_inst->getUses()){
            std::string opstr=use->toStr();
            if(valueTable.find(opstr)==valueTable.end()){
                unsigned num= getValueNumber();
                valueTable[opstr] = num;
                value2operand[num] = use;
            }
            key += std::to_string(valueTable[opstr]);
        }
        if(valueTable.find(key)!=valueTable.end()){
            Operand* dst=it_inst->getDef();
            Type* dst_type= dst->getType()->isAnyInt() ? TypeSystem::constIntType 
                                    : TypeSystem::constFloatType;
            unsigned vnum=valueTable[key];
            Instruction* inst = new BinaryInstruction(BinaryInstruction::ADD,
                dst,new Operand(new ConstantSymbolEntry(dst_type,0)),
                new Operand(*value2operand[vnum]));
            bb->insertBefore(inst,it_inst);
            inst->setNext(it_inst->getNext());
            // delete it_inst; 
            //Didn't delete the instruction, memory leak risk
        }
        valueTable[key] = getValueNumber();
        value2operand[valueTable[key]] = it_inst->getDef();
        valueTable[it_inst->getDef()->toStr()] = valueTable[key];
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
