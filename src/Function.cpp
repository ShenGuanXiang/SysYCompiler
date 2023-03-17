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
            if (v.find(*succ) == v.end())
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
    std::map<BasicBlock *, MachineBlock *> map;

    std::map<BasicBlock *, bool> is_visited;
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
        map[bb] = builder->getBlock();
        for (auto succ = bb->succ_begin(); succ != bb->succ_end(); succ++)
        {
            if (!is_visited[*succ])
            {
                is_visited[*succ] = true;
                q.push_back(*succ);
            }
        }
    }
    cur_func->setEntry(map[entry]);
    assert(entry->predEmpty());

    // for (auto block : block_list)
    // {
    //     block->genMachineCode(builder);
    //     map[block] = builder->getBlock();
    // }

    if (mem2reg) // todo：自动内联后所有参数在IR中都不会被用到，所以都会empty跳过。
    {
        for (auto param : param_list)
        {
            auto id_se = dynamic_cast<IdentifierSymbolEntry *>(param);
            Type *type = param->getType()->isPTR() ? TypeSystem::intType : param->getType();
            if (id_se->getParamOpe()->getUses().empty())
                continue;
            else if (!id_se->paramMem2RegAble() && id_se->getParamNo() < 4)
            {
                assert(id_se->getLabel() != -1);
                auto inst = new MovMInstruction(map[entry], param->getType()->isFloat() ? MovMInstruction::VMOV : MovMInstruction::MOV, new MachineOperand(MachineOperand::VREG, id_se->getLabel(), type), new MachineOperand(MachineOperand::REG, id_se->getParamNo(), type));
                map[entry]->insertBefore(*(map[entry]->getInsts().begin()), inst);
            }
            else if (id_se->getParamNo() > 3)
            {
                int below_dist = 4 * (id_se->getParamNo() - 4);
                auto offset = new MachineOperand(MachineOperand::IMM, below_dist);
                // 由于函数栈帧初始化时会将一些寄存器压栈，在FuncDef打印时还需要偏移一个值，这里保存未偏移前的值，方便后续调整
                cur_func->addAdditionalArgsOffset(offset);
                if (offset->getVal() >= 120) // todo：想办法优化栈偏移合法的情况，窥孔？
                {
                    MachineOperand *internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::intType);
                    auto inst1 = new LoadMInstruction(map[entry], internal_reg, offset);
                    internal_reg = new MachineOperand(*internal_reg);
                    auto inst2 = new LoadMInstruction(map[entry], new MachineOperand(MachineOperand::VREG, id_se->getLabel(), type), new MachineOperand(MachineOperand::REG, 11), internal_reg);
                    map[entry]->insertBefore(*(map[entry]->getInsts().begin()), inst2);
                    map[entry]->insertBefore(*(map[entry]->getInsts().begin()), inst1);
                }
                else
                {
                    auto inst = new LoadMInstruction(map[entry], new MachineOperand(MachineOperand::VREG, id_se->getLabel(), type), new MachineOperand(MachineOperand::REG, 11), offset);
                    map[entry]->insertBefore(*(map[entry]->getInsts().begin()), inst);
                }
            }
        }
    }

    // Add pred and succ for every block
    for (auto block : block_list)
    {
        auto mblock = map[block];
        for (auto pred = block->pred_begin(); pred != block->pred_end(); pred++)
            mblock->addPred(map[*pred]);
        for (auto succ = block->succ_begin(); succ != block->succ_end(); succ++)
            mblock->addSucc(map[*succ]);
    }
    cur_func->addSavedRegs(11); // fp
    cur_unit->insertFunc(cur_func);
}
