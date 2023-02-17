#include "BasicBlock.h"
#include "Function.h"
#include <algorithm>

extern FILE *yyout;

// insert the instruction to the front of the basicblock.
void BasicBlock::insertFront(Instruction *inst)
{
    insertBefore(inst, head->getNext());
}

// insert the instruction to the back of the basicblock.
void BasicBlock::insertBack(Instruction *inst)
{
    insertBefore(inst, head);
}

// insert the instruction dst before src.
void BasicBlock::insertBefore(Instruction *dst, Instruction *src)
{
    Instruction *prev = src->getPrev();
    assert(prev != nullptr);
    prev->setNext(dst);
    dst->setPrev(prev);
    dst->setNext(src);
    src->setPrev(dst);
    dst->setParent(this);
}

// remove the instruction from intruction list.
void BasicBlock::remove(Instruction *inst)
{
    inst->getPrev()->setNext(inst->getNext());
    inst->getNext()->setPrev(inst->getPrev());
}

void BasicBlock::output() const
{
    fprintf(yyout, "B%d:", no);
    fprintf(stderr, "B%d:", no);

    if (!pred.empty())
    {
        auto i = pred.begin();
        fprintf(yyout, "%*c; predecessors = %%B%d", 32, '\t', (*i)->getNo());
        fprintf(stderr, "%*c; predecessors = %%B%d", 32, '\t', (*i)->getNo());
        i++;
        for (; i != pred.end(); i++)
        {
            fprintf(yyout, ", %%B%d", (*i)->getNo());
            fprintf(stderr, ", %%B%d", (*i)->getNo());
        }
    }
    if (!succ.empty())
    {
        auto i = succ.begin();
        fprintf(yyout, "%*c; successors = %%B%d", 32, '\t', (*i)->getNo());
        fprintf(stderr, "%*c; successors = %%B%d", 32, '\t', (*i)->getNo());
        i++;
        for (; i != succ.end(); ++i)
        {
            fprintf(yyout, ", %%B%d", (*i)->getNo());
            fprintf(stderr, ", %%B%d", (*i)->getNo());
        }
    }
    fprintf(yyout, "\n");
    fprintf(stderr, "\n");
    for (auto i = head->getNext(); i != head; i = i->getNext())
        i->output();
    if (head->getNext() == head)
    {
        fprintf(yyout, "  ret void\n");
        fprintf(stderr, "  ret void\n");
    }
}

void BasicBlock::addSucc(BasicBlock *bb)
{
    succ.insert(bb);
}

// remove the successor basicclock bb.
void BasicBlock::removeSucc(BasicBlock *bb)
{
    succ.erase(bb);
}

void BasicBlock::addPred(BasicBlock *bb)
{
    pred.insert(bb);
}

// remove the predecessor basicblock bb.
void BasicBlock::removePred(BasicBlock *bb)
{
    pred.erase(bb);
}

void BasicBlock::genMachineCode(AsmBuilder *builder)
{
    auto cur_func = builder->getFunction();
    auto cur_block = new MachineBlock(cur_func, no);
    builder->setBlock(cur_block);
    for (auto i = head->getNext(); i != head; i = i->getNext())
    {
        i->genMachineCode(builder);
    }
    cur_func->insertBlock(cur_block);
}

BasicBlock::BasicBlock(Function *f)
{
    this->no = SymbolTable::getLabel();
    // if (this->no == 109)
    //     assert(0);
    f->insertBlock(this);
    parent = f;
    head = new DummyInstruction();
    head->setParent(this);
}

BasicBlock::~BasicBlock()
{
    Instruction *inst;
    inst = head->getNext();
    while (inst != head)
    {
        Instruction *t;
        t = inst;
        inst = inst->getNext();
        delete t;
    }
    for (auto &bb : pred)
        bb->removeSucc(this);
    for (auto &bb : succ)
        bb->removePred(this);
    parent->remove(this);
}
