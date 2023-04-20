#include "Function.h"
#include "Unit.h"
#include "Type.h"
#include <list>

extern FILE *yyout;
extern bool mem2reg;

Function::Function(Unit *u, SymbolEntry *s)
{
    u->insertFunc(this);
    entry = new BasicBlock(this);
    sym_ptr = s;
    parent = u;
    ((IdentifierSymbolEntry *)s)->Set_Function(this);
}

Function::~Function()
{
    auto delete_list = block_list;
    for (auto &i : delete_list)
        delete i;
    if (parent != nullptr)
        parent->removeFunc(this);
}

// remove the basicblock bb from its block_list.
void Function::remove(BasicBlock *bb)
{
    std::vector<BasicBlock *> preds(bb->pred_begin(), bb->pred_end()), succs(bb->succ_begin(), bb->succ_end());
    for (auto pred : preds)
        bb->removePred(pred);
    for (auto succ : succs)
        bb->removeSucc(succ);
    auto it = std::find(block_list.begin(), block_list.end(), bb);
    if (it != block_list.end())
        block_list.erase(it);
    bb->setParent(nullptr);
}

void Function::output() const
{
    FunctionType *funcType = dynamic_cast<FunctionType *>(sym_ptr->getType());
    Type *retType = funcType->getRetType();
    fprintf(yyout, "define dso_local %s %s(", retType->toStr().c_str(), sym_ptr->toStr().c_str());
    fprintf(stderr, "define dso_local %s %s(", retType->toStr().c_str(), sym_ptr->toStr().c_str());
    for (auto it = param_list.begin(); it != param_list.end(); it++)
    {
        if (it != param_list.begin())
        {
            fprintf(yyout, ", ");
            fprintf(stderr, ", ");
        }
        fprintf(yyout, "%s %s", (*it)->getType()->toStr().c_str(), (*it)->toStr().c_str());
        fprintf(stderr, "%s %s", (*it)->getType()->toStr().c_str(), (*it)->toStr().c_str());
    }
    fprintf(yyout, ") #0{\n");
    fprintf(stderr, ") #0{\n");
    std::set<BasicBlock *> v;
    std::list<BasicBlock *> q;
    q.push_back(entry);
    v.insert(entry);
    while (!q.empty())
    {
        auto bb = q.front();
        q.pop_front();
        bb->output();
        for (auto succ = bb->succ_begin(); succ != bb->succ_end(); succ++)
        {
            if (!v.count(*succ))
            {
                v.insert(*succ);
                q.push_back(*succ);
            }
        }
    }
    fprintf(yyout, "}\n");
    fprintf(stderr, "}\n");
}

void Function::genMachineCode(AsmBuilder *builder)
{
    auto cur_unit = builder->getUnit();
    std::vector<SymbolEntry *> params_sym_ptr;
    auto cur_func = new MachineFunction(cur_unit, this->sym_ptr);
    builder->setFunction(cur_func);
    std::map<BasicBlock *, MachineBlock *> bb2mbb = std::map<BasicBlock *, MachineBlock *>();

    // find funcCall and save/occupy regs
    cur_func->addSavedRegs(11); // fp
    for (auto &block : getBlockList())
        for (auto inst = block->begin(); inst != block->end(); inst = inst->getNext())
            if (inst->isCall())
            {
                cur_func->addSavedRegs(14); // lr
                auto caller = dynamic_cast<IdentifierSymbolEntry *>(this->getSymPtr());
                auto func_se = dynamic_cast<FuncCallInstruction *>(inst)->getFuncSe();
                if (func_se->isLibFunc())
                    for (auto kv : func_se->getOccupiedRegs())
                        caller->addOccupiedReg(kv.first, kv.second);
                else
                {
                    if (!dynamic_cast<FunctionType *>(func_se->getType())->getRetType()->isVoid())
                        caller->addOccupiedReg(0, Const2Var(dynamic_cast<FunctionType *>(func_se->getType())->getRetType()));
                    auto paramsType = dynamic_cast<FunctionType *>(func_se->getType())->getParamsType();
                    for (int i = 0; i < std::min((int)paramsType.size(), 4); i++)
                        caller->addOccupiedReg(i, paramsType[i]->isARRAY() ? TypeSystem::intType : Const2Var(paramsType[i]));
                    for (auto kv : func_se->getOccupiedRegs())
                        caller->addOccupiedReg(kv.first, kv.second);
                }
            }

    // genMachineCode
    std::map<BasicBlock *, bool> is_visited = std::map<BasicBlock *, bool>();
    std::list<BasicBlock *> q;
    q.push_back(entry);
    for (auto block : block_list)
        is_visited[block] = false;
    is_visited[entry] = true;
    while (!q.empty())
    {
        auto bb = q.front();
        q.pop_front();
        bb->genMachineCode(builder);
        bb2mbb[bb] = builder->getBlock();
        for (auto succ = bb->succ_begin(); succ != bb->succ_end(); succ++)
        {
            if (!is_visited[*succ])
            {
                is_visited[*succ] = true;
                q.push_back(*succ);
            }
        }
    }
    auto block_list_copy = block_list;
    for (auto &block : block_list_copy)
    {
        if (!is_visited[block])
            delete block;
    }
    cur_func->setEntry(bb2mbb[entry]);
    assert(entry->predEmpty());

    // load params to reg
    if (mem2reg)
    {
        for (auto &param : param_list)
        {
            auto id_se = dynamic_cast<IdentifierSymbolEntry *>(param);
            Type *type = param->getType()->isPTR() ? TypeSystem::intType : param->getType();
            if (id_se->getParamOpe()->usersNum() == 0 || id_se->getLabel() == -1)
                // todo: 不写后一个条件有奇怪的bug，比如这个例子：
                // int f(float a, float b, int c, int d, float e, float g, float h)
                // {
                //   getfloat();
                //   putint(c);
                //   putfloat(e);
                //   return 0;
                // }
                //
                // int main()
                // {
                //   return f(65,66,67,68,69,70,71);
                // }
                continue;
            else if (!id_se->paramMem2RegAble() && id_se->getParamNo() < 4)
            {
                assert(id_se->getLabel() != -1);
                auto inst = new MovMInstruction(bb2mbb[entry], param->getType()->isFloat() ? MovMInstruction::VMOV : MovMInstruction::MOV, new MachineOperand(MachineOperand::VREG, id_se->getLabel(), type), new MachineOperand(MachineOperand::REG, id_se->getParamNo(), type));
                bb2mbb[entry]->insertBefore(*(bb2mbb[entry]->getInsts().begin()), inst);
            }
            else if (id_se->getParamNo() > 3)
            {
                int below_dist = 4 * (id_se->getParamNo() - 4);
                auto offset = new MachineOperand(MachineOperand::IMM, below_dist);
                // 由于函数栈帧初始化时会将一些寄存器压栈，在FuncDef打印时还需要偏移一个值，这里保存未偏移前的值，方便后续调整
                cur_func->addAdditionalArgsOffset(offset);
                if (offset->getVal() >= 852) // todo：想办法优化栈偏移合法的情况，寄存器分配后再窥孔？
                {
                    MachineOperand *internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::intType);
                    auto inst1 = new LoadMInstruction(bb2mbb[entry], internal_reg, offset);
                    internal_reg = new MachineOperand(*internal_reg);
                    if (type->isFloat())
                    {
                        auto internal_reg1 = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::intType);
                        auto inst2 = new BinaryMInstruction(bb2mbb[entry], BinaryMInstruction::ADD, internal_reg1, new MachineOperand(MachineOperand::REG, 11), internal_reg);
                        auto inst3 = new LoadMInstruction(bb2mbb[entry], new MachineOperand(MachineOperand::VREG, id_se->getLabel(), type), new MachineOperand(*internal_reg1));
                        bb2mbb[entry]->insertBefore(*bb2mbb[entry]->getInsts().begin(), inst3);
                        bb2mbb[entry]->insertBefore(*bb2mbb[entry]->getInsts().begin(), inst2);
                    }
                    else
                    {
                        auto inst2 = new LoadMInstruction(bb2mbb[entry], new MachineOperand(MachineOperand::VREG, id_se->getLabel(), type), new MachineOperand(MachineOperand::REG, 11), internal_reg);
                        bb2mbb[entry]->insertBefore(*bb2mbb[entry]->getInsts().begin(), inst2);
                    }
                    bb2mbb[entry]->insertBefore(*bb2mbb[entry]->getInsts().begin(), inst1);
                }
                else
                {
                    auto inst = new LoadMInstruction(bb2mbb[entry], new MachineOperand(MachineOperand::VREG, id_se->getLabel(), type), new MachineOperand(MachineOperand::REG, 11), offset);
                    bb2mbb[entry]->insertBefore(*bb2mbb[entry]->getInsts().begin(), inst);
                }
            }
        }
    }

    // Add pred and succ for every block
    for (auto &block : block_list)
    {
        auto mblock = bb2mbb[block];
        for (auto pred = block->pred_begin(); pred != block->pred_end(); pred++)
            mblock->addPred(bb2mbb[*pred]);
        for (auto succ = block->succ_begin(); succ != block->succ_end(); succ++)
            mblock->addSucc(bb2mbb[*succ]);
    }

    // mov sp, fp for large stack
    if (cur_func->AllocSpace(0) > 200)
    {
        cur_func->setLargeStack();
        for (auto &block : cur_func->getBlocks())
            for (auto &inst : block->getInsts())
                if (inst->isDummy())
                {
                    assert(inst == *block->getInsts().rbegin());
                    inst->addUse(new MachineOperand(MachineOperand::REG, 11)); // fp
                }
    }

    // pretend to define some regs
    std::vector<MachineOperand *> params_mope;
    for (auto &param : param_list)
    {
        auto id_se = dynamic_cast<IdentifierSymbolEntry *>(param);
        Type *type = param->getType()->isPTR() ? TypeSystem::intType : param->getType();
        if (id_se->getParamNo() < 4)
            params_mope.push_back(new MachineOperand(MachineOperand::REG, id_se->getParamNo(), type));
    }
    auto dummy = new DummyMInstruction(cur_func->getEntry(), params_mope, std::vector<MachineOperand *>());
    dummy->addDef(new MachineOperand(MachineOperand::REG, 11)); // fp
    cur_func->getEntry()->insertBefore(*cur_func->getEntry()->getInsts().begin(), dummy);

    cur_unit->insertFunc(cur_func);
}
