#include "ComSubExprElim.h"
#include "Instruction.h"
#include "SymbolTable.h"

#include <vector>
#include <queue>

std::string ComSubExprElim::getOpString(Instruction *inst)
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
        default:
            assert(0);
        }
        break;
    case Instruction::GEP:
        instString += "GEP";
        break;
    case Instruction::PHI:
        instString += "PHI";
        break;
    case Instruction::IFCAST:
        switch (dynamic_cast<IntFloatCastInstruction *>(inst)->getOpcode())
        {
        case IntFloatCastInstruction::S2F:
            instString += "SF";
            break;
        case IntFloatCastInstruction::F2S:
            instString += "FS";
            break;
        default:
            assert(0);
        }
        break;
    default:
        break;
    }
    if (instString == "PHI")
    {
        auto phi = dynamic_cast<PhiInstruction *>(inst);
        auto args = phi->getSrcs();

        bool meaningless = true;
        Operand *prearg = phi->getUses()[0];

        for (auto it_arg = args.begin(); it_arg != args.end(); it_arg++)
        {
            // 防止全局变量的名字对key进行hack
            std::string argstr = ",%B" + std::to_string(it_arg->first->getNo()) + "," + it_arg->second->toStr();
            if (prearg != it_arg->second)
                meaningless = false;
            else
                prearg = it_arg->second;
            instString += argstr;
        }
        if (meaningless)
        {
            return "MEANINGLESS_PHI";
        }
    }
    else if (instString != "")
    {
        for (auto use : inst->getUses())
        {
            if (!htable.count(use->toStr()))
                instString += "," + use->toStr();
            else
                instString += htable[use->toStr()]->toStr();
        }
    }
    return instString;
}

std::string ComSubExprElim::getIdentity(Instruction *inst)
{
    std::string instString = "";
    if (inst->isBinary())
    {
        BinaryInstruction *bin = dynamic_cast<BinaryInstruction *>(inst);
        if (bin->getOpcode() == BinaryInstruction::ADD)
            instString += "ADD";
        else if (bin->getOpcode() == BinaryInstruction::MUL)
            instString += "MUL";
        if (instString != "")
        {
            assert(bin->getUses().size() == 2);
            instString += "," + bin->getUses()[1]->toStr();
            instString += "," + bin->getUses()[0]->toStr();
        }
    }
    return instString;
}

void ComSubExprElim::dumpTable()
{
    printf("------\n");
    for (auto p = htable.begin(); p != htable.end(); p++)
        printf("%s->%s\n", p->first.c_str(), p->second->toStr().c_str());
    printf("------\n");
}

void ComSubExprElim::computeDomTree(Function *func)
{
    domtree.clear();
    func->ComputeDom();
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        BasicBlock *bb = *it_bb;
        if (bb == func->getEntry())
            continue;
        domtree[bb->getIDom()].push_back(bb);
    }
}

void ComSubExprElim::dvnt(BasicBlock *bb)
{
    std::unordered_map<std::string, Operand *> prehtable;
    prehtable = htable;              // store curent htable, to restore after processing children
    std::vector<Instruction *> torm; // instruction to remove
    for (auto cur_inst = bb->begin(); cur_inst != bb->end(); cur_inst = cur_inst->getNext())
    {
        std::string instString;
        instString += getOpString(cur_inst);
        if (instString == "")
            continue;
        Operand *dst = cur_inst->getDef();
        if (instString == "MEANINGLESS_PHI")
        {
            auto args = dynamic_cast<PhiInstruction *>(cur_inst)->getSrcs();
            htable[dst->toStr()] = args.begin()->second;
            torm.push_back(cur_inst);
            cur_inst->replaceAllUsesWith(args.begin()->second);
        }
        else if (instString.substr(0, 3) == "PHI" &&
                 htable.count(instString) &&
                 htable[instString]->getDef()->getParent() == bb)
        {
            // redundant
            htable[dst->toStr()] = htable[instString];
            torm.push_back(cur_inst);
            cur_inst->replaceAllUsesWith(htable[instString]);
        }
        else
        {
            // 注意这个lvn只用于gcm，gcm不能将faulting instruction（DIV）
            // 向前移动，因此这里也不能消除
            if (htable.count(instString) && instString.substr(0, 3) != "DIV" && instString.substr(0, 3) != "MOD")
            {
                auto src = htable[instString];
                torm.push_back(cur_inst);
                htable[dst->toStr()] = src;
                cur_inst->replaceAllUsesWith(src);
            }
            else
            {
                htable[instString] = dst;
                std::string identity = getIdentity(cur_inst);
                if (identity != "")
                {
                    htable[identity] = dst;
                }
            }
        }
    }
    for (auto i : torm)
    {
        bb->remove(i);
        delete i;
    }
    // 倒着遍历支配树中的子节点，使得带有phi语句的块尽量靠后处理，尽可能消除phi语句
    // 算法不保证消除所有不必要的phi语句，因为遍历phi语句时，其操作数可能没被更新
    for (auto it_child = domtree[bb].rbegin(); it_child != domtree[bb].rend(); it_child++)
        dvnt(*it_child);

    // deallocate context for this basic block
    htable = prehtable;
}

void ComSubExprElim::pass3()
{
    for (auto it_func = unit->begin(); it_func != unit->end(); it_func++)
    {
        auto entry = (*it_func)->getEntry();
        computeDomTree(*it_func);
        dvnt(entry);
    }
}

void ComSubExprElim::pass1(BasicBlock *bb)
{
    std::vector<Instruction *> torm; // instruction to remove
    for (auto cur_inst = bb->begin(); cur_inst != bb->end(); cur_inst = cur_inst->getNext())
    {
        std::string instString;
        instString += getOpString(cur_inst);
        if (instString == "")
            continue;
        Operand *dst = cur_inst->getDef();
        if (instString == "MEANINGLESS_PHI")
        {
            auto args = dynamic_cast<PhiInstruction *>(cur_inst)->getSrcs();
            htable[dst->toStr()] = args.begin()->second;
            torm.push_back(cur_inst);
            cur_inst->replaceAllUsesWith(args.begin()->second);
            fprintf(stderr, "[GVN]:meanless phi\n");
        }
        else if (instString.substr(0, 3) == "PHI" &&
                 htable.count(instString) &&
                 htable[instString]->getDef()->getParent() == bb)
        {
            // redundant
            htable[dst->toStr()] = htable[instString];
            torm.push_back(cur_inst);
            cur_inst->replaceAllUsesWith(htable[instString]);
            fprintf(stderr, "[GVN]:redundant phi\n");
        }
        else
        {
            if (htable.count(instString) &&
                instString.substr(0, 3) != "DIV" && instString.substr(0, 3) != "MOD")
            {
                if (instString.substr(0, 3) == "PHI")
                    continue; // we cannot elminate phi here
                auto src = htable[instString];
                torm.push_back(cur_inst);
                htable[dst->toStr()] = src;
                cur_inst->replaceAllUsesWith(src);
                fprintf(stderr, "[GVN]:%s->%s\n", dst->toStr().c_str(), src->toStr().c_str());
            }
            else
            {
                htable[instString] = dst;
                std::string identity = getIdentity(cur_inst);
                if (identity != "")
                {
                    htable[identity] = dst;
                }
            }
        }
    }
    if (torm.size())
        fprintf(stderr, "[GVN]: %zu\n", torm.size());
    for (auto i : torm)
    {
        bb->remove(i);
        delete i;
    }
}

// the following functions are used for value numbering in assmbly code generation phase

void ComSubExprElimASM::computeDomTree(MachineFunction *func)
{
    domtree.clear();
    func->computeDom();
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        MachineBlock *bb = *it_bb;
        if (bb != func->getEntry())
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

std::string ComSubExprElimASM::getOpString(MachineInstruction *minst, bool lvn)
{

    std::string instString = "";
    if (minst->getDef().empty() || minst->getDef().size() > 1)
        return instString;

    for (auto use : minst->getUse())
        if (minst->getParent()->getParent()->getAdditionalArgsOffset().count(use) && !after_regAlloc)
            return instString;

    if (!lvn)
    {
        // 忽略带有条件的指令，这种指令不能被消除，但是其操作数应该被替换
        if (minst->getCond() != MachineInstruction::NONE)
            return instString;

        // overlook minst that uses redefined operand
        for (auto use : minst->getUse())
            if (redef.count(*use))
                return instString;

        if (redef.count(*minst->getDef()[0]))
            return instString;
    }

    switch (minst->getInstType())
    {
    case MachineInstruction::LOAD: // for load imm
        if ((minst->getUse().size() == 1) && minst->getUse()[0]->isImm())
            instString += "LOADI";
        else if ((minst->getUse().size() == 1) && minst->getUse()[0]->isLabel())
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
        case BinaryMInstruction::ADDASR:
            instString += "ADDASR";
            break;
        case BinaryMInstruction::ADDLSL:
            instString += "ADDLSL";
            break;
        case BinaryMInstruction::ADDLSR:
            instString += "ADDLSR";
            break;
        case BinaryMInstruction::SUBASR:
            instString += "SUBASR";
            break;
        case BinaryMInstruction::SUBLSL:
            instString += "SUBLSL";
            break;
        case BinaryMInstruction::SUBLSR:
            instString += "SUBLSR";
            break;
        case BinaryMInstruction::RSBASR:
            instString += "RSBASR";
            break;
        case BinaryMInstruction::RSBLSL:
            instString += "RSBLSL";
            break;
        case BinaryMInstruction::RSBLSR:
            instString += "RSBLSR";
            break;
        default:
            assert(0);
        }
        break;
    case MachineInstruction::MOV:
        switch (dynamic_cast<MovMInstruction *>(minst)->getOpType())
        {
        case MovMInstruction::MOV:
            instString = "MOV";
            break;
        case MovMInstruction::VMOV:
            instString = "VMOV";
            break;
        case MovMInstruction::MOVLSL:
            instString = "MOVLSL";
            break;
        case MovMInstruction::MOVLSR:
            instString = "MOVLSR";
            break;
        case MovMInstruction::MOVASR:
            instString = "MOVASR";
            break;
        default:
            assert(0);
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

void ComSubExprElimASM::pass()
{
    for (auto it_mfunc = munit->begin(); it_mfunc != munit->end(); it_mfunc++)
    {
        auto entry = (*it_mfunc)->getEntry();
        computeDomTree(*it_mfunc);
        findredef(entry);
        dvnt(entry);
    }
    for (auto i : freeInsts)
    {
        delete i;
    }
    freeInsts.clear();
}
/*
后端cse：
    第一次遍历全局消去SSA的公共子表达式，第二次遍历局部消去非SSA的公共子表达式
*/
void ComSubExprElimASM::dvnt(MachineBlock *bb)
{
    std::unordered_map<std::string, MachineOperand *> prehtable;

    prehtable = htable; // store current htable, to restore after processing children

    std::vector<MachineInstruction *> torm;

    for (auto it_minst = bb->begin(); it_minst != bb->end(); it_minst++)
    {
        auto inst = *it_minst;
        // replace
        for (auto &use : inst->getUse())
            if (htable.count(use->toStr()))
            {
                assert(!redef.count(*use));
                use = new MachineOperand(*htable[use->toStr()]);
                use->setParent(inst);
            }

        std::string instString = getOpString(inst);
        if (instString == "")
            continue;

        // get instString
        for (auto use : inst->getUse())
        {
            auto usestr = use->toStr();
            if (htable.count(usestr))
                instString += "," + htable[usestr]->toStr();
            else
                instString += "," + usestr;
        }

        auto dst = inst->getDef()[0];

        // redundant mov/loadimm whose dse is redefined can only be removed by inserting mov, unable to remove

        if (htable.count(instString))
        {

            // if (inst->getDef()[0]->isReg() ||
            //     (inst->getInstType() == MachineInstruction::MOV && redef.count(*dst)) ||
            //     (inst->getInstType() == MachineInstruction::LOAD && redef.count(*dst) && (inst->getUse().size() == 1) && inst->getUse()[0]->isImm()) ||
            //     (inst->getInstType() == MachineInstruction::LOAD && redef.count(*dst) && (inst->getUse().size() == 1) && inst->getUse()[0]->isLabel()))
            // {
            //     continue;
            // }

            auto src = htable[instString];
            htable[dst->toStr()] = src;

            // if (redef.count(*dst))
            // {
            //     MachineInstruction *mov;
            //     if (dst->getValType()->isFloat())
            //         mov = new MovMInstruction(bb, MovMInstruction::VMOV, dst, src);
            //     else
            //         mov = new MovMInstruction(bb, MovMInstruction::MOV, dst, src);
            //     bb->insertAfter(inst, mov);
            //     it_minst++;
            // }
            // else
            torm.push_back(inst);
        }
        else
        {
            htable[instString] = dst;
            // if(instString.substr(0,3) == "ADD" || instString.substr(0,3) == "MUL"){
            //     fprintf(stderr,"[GVN]:%s->%s\n",instString.c_str(),dst->toStr().c_str());
            //     assert(inst->getUse().size() == 2);
            //     std::string identity = instString.substr(0,3)
            //     + "," + inst->getUse()[1]->toStr()
            //     + "," + inst->getUse()[0]->toStr();
            //     htable[identity] = dst;
            // }
        }
    }

    for (auto i : torm)
    {
        bb->removeInst(i);
        freeInsts.insert(i);
    }

    lvn(bb);

    for (auto mb : domtree[bb])
        dvnt(mb);

    htable = prehtable;
}

void ComSubExprElimASM::lvn(MachineBlock *bb)
{
    unsigned long long val = 0;
    std::unordered_map<std::string, unsigned long long> inst2val;
    std::unordered_map<MachineOperand *, unsigned long long> op2val;
    std::vector<std::set<MachineOperand *>> val2ops;
    std::vector<MachineInstruction *> torm;
    for (auto it_inst = bb->begin(); it_inst != bb->end(); it_inst++)
    {
        auto minst = *it_inst;
        auto inst_str = getOpString(minst, true);
        if (inst_str == "")
            continue;

        for (auto use : minst->getUse())
        {
            if (!op2val.count(use))
            {
                op2val[use] = val;
                val2ops.push_back({use});
                val++;
            }
            inst_str += "," + std::to_string(op2val[use]);
        }

        auto dst = minst->getDef()[0];
        if (op2val.count(dst))
        {
            unsigned long long dstval = op2val[dst];
            auto it = val2ops[dstval].find(dst);
            val2ops[dstval].erase(it);
        }

        if (inst2val.count(inst_str))
        {
            unsigned long long instval = inst2val[inst_str];
            if (val2ops[instval].empty())
                continue;
            auto src = new MachineOperand(**val2ops[instval].begin());
            dst = new MachineOperand(*dst);
            // new mov & delete
            torm.push_back(minst);
            MachineInstruction *mov;
            if (dst->getValType()->isFloat())
                mov = new MovMInstruction(bb, MovMInstruction::VMOV, dst, src);
            else
                mov = new MovMInstruction(bb, MovMInstruction::MOV, dst, src);
            bb->insertBefore(minst, mov);
            op2val[dst] = instval;
            val2ops[instval].insert(dst);
        }
        else
        {
            inst2val[inst_str] = val;
            op2val[dst] = val;
            val2ops.push_back({dst});
            val++;
        }
    }
    for (auto i : torm)
    {
        bb->removeInst(i);
        freeInsts.insert(i);
    }
}

void ComSubExprElimASM::dumpTable()
{
    printf("------\n");
    for (auto it = htable.begin(); it != htable.end(); it++)
        std::cout << it->first << " " << it->second->toStr() << std::endl;
    printf("------\n");
}

void ComSubExprElimASM::findredef(MachineBlock *entry)
{
    // bfs from the entry
    // defset.clear();
    // redef.clear();
    std::queue<MachineBlock *> q;
    std::unordered_set<MachineBlock *> visited;
    q.push(entry);
    visited.insert(entry);
    while (!q.empty())
    {
        auto bb = q.front();
        q.pop();
        for (auto it_minst = bb->begin(); it_minst != bb->end(); it_minst++)
        {
            auto inst = *it_minst;

            for (auto &def : inst->getDef())
                if (def->isVReg() || def->isReg())
                {
                    if (defset.count(*def))
                        redef.insert(*def);
                    else
                        defset.insert(*def);
                }
        }
        for (auto succ : bb->getSuccs())
            if (visited.count(succ) == 0)
            {
                q.push(succ);
                visited.insert(succ);
            }
    }
}