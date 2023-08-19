#include "AlgSimplify.h"
// #include <tuple>

// TODO：记录更深层，看更远

void AlgSimplify::pass()
{
    for (auto func : unit->getFuncList())
        pass(func);
}

static inline bool equal(Operand *op1, Operand *op2)
{
    return (op1 == op2) || (op1->getType()->isConst() && op2->getType()->isConst() && op1->getEntry()->getValue() == op2->getEntry()->getValue());
}

static inline bool equal(std::tuple<int, Operand *, Operand *> tuple1, std::tuple<int, Operand *, Operand *> tuple2)
{
    return std::get<0>(tuple1) == std::get<0>(tuple2) && ((equal(std::get<1>(tuple1), std::get<1>(tuple2)) && equal(std::get<2>(tuple1), std::get<2>(tuple2))) || ((std::get<0>(tuple1) == BinaryInstruction::ADD || std::get<0>(tuple1) == BinaryInstruction::MUL) && equal(std::get<1>(tuple1), std::get<2>(tuple2)) && equal(std::get<2>(tuple1), std::get<1>(tuple2))));
}

// static inline void printExpr(std::tuple<int, Operand *, Operand *> expr)
// {
//     fprintf(stderr, "%s\t", std::get<1>(expr)->toStr().c_str());
//     switch (std::get<0>(expr))
//     {
//     case BinaryInstruction::ADD:
//         fprintf(stderr, "+");
//         break;
//     case BinaryInstruction::SUB:
//         fprintf(stderr, "-");
//         break;
//     case BinaryInstruction::MUL:
//         fprintf(stderr, "*");
//         break;
//     case BinaryInstruction::DIV:
//         fprintf(stderr, "/");
//         break;
//     case BinaryInstruction::MOD:
//         fprintf(stderr, "%%");
//         break;
//     }
//     fprintf(stderr, "\t%s\n", std::get<2>(expr)->toStr().c_str());
// }

void AlgSimplify::pass(Function *func)
{
    bool change = false;
    std::map<Operand *, std::tuple<int, Operand *, Operand *>> binary_expr;
    do
    {
        change = false;
        auto all_bbs = func->getBlockList();
        for (auto bb : all_bbs)
        {
            std::set<Instruction *> freeInsts;
            for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
            {
                if (inst->isBinary())
                {
                    // inst->output();
                    if (inst->getUses()[0]->getType()->isConst() && inst->getUses()[0]->getEntry()->getValue() == 0)
                    {
                        switch (inst->getOpcode())
                        {
                        case BinaryInstruction::ADD:
                        {
                            inst->replaceAllUsesWith(inst->getUses()[1]);
                            freeInsts.insert(inst);
                            break;
                        }
                        case BinaryInstruction::DIV:
                        case BinaryInstruction::MUL:
                        case BinaryInstruction::MOD:
                        {
                            inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 0)));
                            freeInsts.insert(inst);
                            break;
                        }
                        default:
                            break;
                        }
                    }
                    else if (inst->getUses()[1]->getType()->isConst() && inst->getUses()[1]->getEntry()->getValue() == 0)
                    {
                        switch (inst->getOpcode())
                        {
                        case BinaryInstruction::ADD:
                        case BinaryInstruction::SUB:
                        {
                            inst->replaceAllUsesWith(inst->getUses()[0]);
                            freeInsts.insert(inst);
                            break;
                        }
                        case BinaryInstruction::MUL:
                        {
                            inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 0)));
                            freeInsts.insert(inst);
                            break;
                        }
                        default:
                            break;
                        }
                    }
                    else if (inst->getUses()[0]->getType()->isConst() && inst->getUses()[0]->getEntry()->getValue() == 1)
                    {
                        switch (inst->getOpcode())
                        {
                        case BinaryInstruction::MUL:
                        {
                            inst->replaceAllUsesWith(inst->getUses()[1]);
                            freeInsts.insert(inst);
                            break;
                        }
                        default:
                            break;
                        }
                    }
                    else if (inst->getUses()[1]->getType()->isConst() && inst->getUses()[1]->getEntry()->getValue() == 1)
                    {
                        switch (inst->getOpcode())
                        {
                        case BinaryInstruction::MUL:
                        case BinaryInstruction::DIV:
                        {
                            inst->replaceAllUsesWith(inst->getUses()[0]);
                            freeInsts.insert(inst);
                            break;
                        }
                        case BinaryInstruction::MOD:
                        {
                            inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 0)));
                            freeInsts.insert(inst);
                            break;
                        }
                        default:
                            break;
                        }
                    }
                    else if (equal(inst->getUses()[0], inst->getUses()[1]))
                    {
                        switch (inst->getOpcode())
                        {
                        case BinaryInstruction::ADD:
                        {
                            freeInsts.insert(inst);
                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), inst->getUses()[0], new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 2))));
                            break;
                        }
                        case BinaryInstruction::SUB:
                        {
                            freeInsts.insert(inst);
                            inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 0)));
                            break;
                        }
                        case BinaryInstruction::DIV:
                        {
                            freeInsts.insert(inst);
                            inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 1)));
                            break;
                        }
                        case BinaryInstruction::MOD:
                        {
                            freeInsts.insert(inst);
                            inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 0)));
                            break;
                        }
                        default:
                            break;
                        }
                    }

                    if (!freeInsts.count(inst) && inst->getDef()->getType()->isInt())
                    {
                        switch (inst->getOpcode())
                        {
                        case BinaryInstruction::ADD:
                        {
                            // const1_ + ...
                            if (inst->getUses()[0]->getType()->isConst())
                            {
                                if (binary_expr.count(inst->getUses()[1]))
                                {
                                    switch (std::get<0>(binary_expr[inst->getUses()[1]]))
                                    {
                                    case BinaryInstruction::ADD:
                                    {
                                        // const1_ + (const21 + a22)
                                        if (std::get<1>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), std::get<2>(binary_expr[inst->getUses()[1]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() + std::get<1>(binary_expr[inst->getUses()[1]])->getEntry()->getValue()))));
                                        }
                                        // const1_ + (a21 + const22)
                                        else if (std::get<2>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), std::get<1>(binary_expr[inst->getUses()[1]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() + std::get<2>(binary_expr[inst->getUses()[1]])->getEntry()->getValue()))));
                                        }
                                        break;
                                    }
                                    case BinaryInstruction::SUB:
                                    {
                                        // const1_ + (const21 - a22)
                                        if (std::get<1>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, inst->getDef(), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() + std::get<1>(binary_expr[inst->getUses()[1]])->getEntry()->getValue())), std::get<2>(binary_expr[inst->getUses()[1]])));
                                        }
                                        // const1_ + (a21 - const22)
                                        else if (std::get<2>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), std::get<1>(binary_expr[inst->getUses()[1]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() - std::get<2>(binary_expr[inst->getUses()[1]])->getEntry()->getValue()))));
                                        }
                                        break;
                                    }
                                    default:
                                        break;
                                    }
                                }
                            }
                            // ... + const2_
                            else if (inst->getUses()[1]->getType()->isConst())
                            {
                                if (binary_expr.count(inst->getUses()[0]))
                                {
                                    switch (std::get<0>(binary_expr[inst->getUses()[0]]))
                                    {
                                    case BinaryInstruction::ADD:
                                    {
                                        // (const11 + a12) + const2_
                                        if (std::get<1>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), std::get<2>(binary_expr[inst->getUses()[0]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[1]->getEntry()->getValue() + std::get<1>(binary_expr[inst->getUses()[0]])->getEntry()->getValue()))));
                                        }
                                        // (a11 + const12) + const22
                                        else if (std::get<2>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), std::get<1>(binary_expr[inst->getUses()[0]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[1]->getEntry()->getValue() + std::get<2>(binary_expr[inst->getUses()[0]])->getEntry()->getValue()))));
                                        }
                                        break;
                                    }
                                    case BinaryInstruction::SUB:
                                    {
                                        // (const11 - a12) + const2_
                                        if (std::get<1>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, inst->getDef(), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[1]->getEntry()->getValue() + std::get<1>(binary_expr[inst->getUses()[0]])->getEntry()->getValue())), std::get<2>(binary_expr[inst->getUses()[0]])));
                                        }
                                        // (a11 - const12) + const2_
                                        else if (std::get<2>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), std::get<1>(binary_expr[inst->getUses()[0]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[1]->getEntry()->getValue() - std::get<2>(binary_expr[inst->getUses()[0]])->getEntry()->getValue()))));
                                        }
                                        break;
                                    }
                                    default:
                                        break;
                                    }
                                }
                            }
                            // ... + ...
                            else
                            {
                                if (binary_expr.count(inst->getUses()[0]) && binary_expr.count(inst->getUses()[1]))
                                {
                                    // a*b + b*a、a+b + b+a
                                    if (equal(binary_expr[inst->getUses()[0]], binary_expr[inst->getUses()[1]]))
                                    {
                                        freeInsts.insert(inst);
                                        inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), inst->getUses()[0], new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 2))));
                                    }
                                    // a*b + a*c
                                    else if (std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::MUL && std::get<0>(binary_expr[inst->getUses()[1]]) == BinaryInstruction::MUL)
                                    {
                                        if (inst->getUses()[0]->getUses().size() == 1 && inst->getUses()[1]->getUses().size() == 1)
                                        {
                                            Operand *commonFactor = nullptr, *op1 = nullptr, *op2 = nullptr;
                                            if (equal(std::get<1>(binary_expr[inst->getUses()[0]]), std::get<1>(binary_expr[inst->getUses()[1]])))
                                            {
                                                commonFactor = std::get<1>(binary_expr[inst->getUses()[0]]);
                                                op1 = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                op2 = std::get<2>(binary_expr[inst->getUses()[1]]);
                                            }
                                            else if (equal(std::get<1>(binary_expr[inst->getUses()[0]]), std::get<2>(binary_expr[inst->getUses()[1]])))
                                            {
                                                commonFactor = std::get<1>(binary_expr[inst->getUses()[0]]);
                                                op1 = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                op2 = std::get<1>(binary_expr[inst->getUses()[1]]);
                                            }
                                            else if (equal(std::get<2>(binary_expr[inst->getUses()[0]]), std::get<1>(binary_expr[inst->getUses()[1]])))
                                            {
                                                commonFactor = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                op1 = std::get<1>(binary_expr[inst->getUses()[0]]);
                                                op2 = std::get<2>(binary_expr[inst->getUses()[1]]);
                                            }
                                            else if (equal(std::get<2>(binary_expr[inst->getUses()[0]]), std::get<2>(binary_expr[inst->getUses()[1]])))
                                            {
                                                commonFactor = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                op1 = std::get<1>(binary_expr[inst->getUses()[0]]);
                                                op2 = std::get<1>(binary_expr[inst->getUses()[1]]);
                                            }
                                            if (commonFactor != nullptr)
                                            {
                                                freeInsts.insert(inst);
                                                auto temp = new Operand(new TemporarySymbolEntry(inst->getDef()->getType(), SymbolTable::getLabel()));
                                                bb->insertAfter(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), temp, commonFactor), inst);
                                                inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, temp, op1, op2));
                                            }
                                        }
                                    }
                                    // (a + b) + (c - a)
                                    else if (std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::ADD && std::get<0>(binary_expr[inst->getUses()[1]]) == BinaryInstruction::SUB &&
                                             (equal(std::get<2>(binary_expr[inst->getUses()[1]]), std::get<1>(binary_expr[inst->getUses()[0]])) || equal(std::get<2>(binary_expr[inst->getUses()[1]]), std::get<2>(binary_expr[inst->getUses()[0]]))))
                                    {
                                        freeInsts.insert(inst);
                                        Operand *adder1 = std::get<1>(binary_expr[inst->getUses()[1]]), *adder2 = nullptr;
                                        if (equal(std::get<2>(binary_expr[inst->getUses()[1]]), std::get<1>(binary_expr[inst->getUses()[0]])))
                                            adder2 = std::get<2>(binary_expr[inst->getUses()[0]]);
                                        else
                                        {
                                            assert(equal(std::get<2>(binary_expr[inst->getUses()[1]]), std::get<2>(binary_expr[inst->getUses()[0]])));
                                            adder2 = std::get<1>(binary_expr[inst->getUses()[0]]);
                                        }
                                        inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), adder1, adder2));
                                    }
                                    // (a - b) + (c - a)
                                    else if (std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::SUB && std::get<0>(binary_expr[inst->getUses()[1]]) == BinaryInstruction::SUB &&
                                             equal(std::get<2>(binary_expr[inst->getUses()[1]]), std::get<1>(binary_expr[inst->getUses()[0]])))
                                    {
                                        freeInsts.insert(inst);
                                        Operand *substracted = std::get<1>(binary_expr[inst->getUses()[1]]), *substractor = std::get<2>(binary_expr[inst->getUses()[0]]);
                                        inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, inst->getDef(), substracted, substractor));
                                    }
                                }
                                else if (binary_expr.count(inst->getUses()[0]))
                                {
                                    // (a - b) + b
                                    if (std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::SUB && equal(std::get<2>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]))
                                    {
                                        freeInsts.insert(inst);
                                        inst->replaceAllUsesWith(std::get<1>(binary_expr[inst->getUses()[0]]));
                                    }
                                    // const * a + a
                                    else if (std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::MUL && inst->getUses()[0]->getUses().size() == 1 &&
                                             ((equal(std::get<1>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]) && std::get<2>(binary_expr[inst->getUses()[0]])->getType()->isConst()) ||
                                              (equal(std::get<2>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]) && std::get<1>(binary_expr[inst->getUses()[0]])->getType()->isConst())))
                                    {
                                        freeInsts.insert(inst);
                                        Operand *multiplier1 = inst->getUses()[1], *multiplier2 = nullptr;
                                        if (equal(std::get<1>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]))
                                        {
                                            multiplier2 = new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), std::get<2>(binary_expr[inst->getUses()[0]])->getEntry()->getValue() + 1));
                                        }
                                        else
                                        {
                                            multiplier2 = new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), std::get<1>(binary_expr[inst->getUses()[0]])->getEntry()->getValue() + 1));
                                        }
                                        inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), multiplier1, multiplier2));
                                    }
                                }
                                else if (binary_expr.count(inst->getUses()[1]))
                                {
                                    // a + (b - a)
                                    if (std::get<0>(binary_expr[inst->getUses()[1]]) == BinaryInstruction::SUB && equal(std::get<2>(binary_expr[inst->getUses()[1]]), inst->getUses()[0]))
                                    {
                                        freeInsts.insert(inst);
                                        inst->replaceAllUsesWith(std::get<1>(binary_expr[inst->getUses()[1]]));
                                    }
                                    // a + a * const
                                    else if (std::get<0>(binary_expr[inst->getUses()[1]]) == BinaryInstruction::MUL && inst->getUses()[1]->getUses().size() == 1 &&
                                             ((equal(std::get<1>(binary_expr[inst->getUses()[1]]), inst->getUses()[0]) && std::get<2>(binary_expr[inst->getUses()[1]])->getType()->isConst()) ||
                                              (equal(std::get<2>(binary_expr[inst->getUses()[1]]), inst->getUses()[0]) && std::get<1>(binary_expr[inst->getUses()[1]])->getType()->isConst())))
                                    {
                                        freeInsts.insert(inst);
                                        Operand *multiplier1 = inst->getUses()[0], *multiplier2 = nullptr;
                                        if (equal(std::get<1>(binary_expr[inst->getUses()[1]]), inst->getUses()[0]))
                                        {
                                            multiplier2 = new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), std::get<2>(binary_expr[inst->getUses()[1]])->getEntry()->getValue() + 1));
                                        }
                                        else
                                        {
                                            multiplier2 = new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), std::get<1>(binary_expr[inst->getUses()[1]])->getEntry()->getValue() + 1));
                                        }
                                        inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), multiplier1, multiplier2));
                                    }
                                }
                            }
                            break;
                        }
                        case BinaryInstruction::SUB:
                        {
                            // const1_ - ...
                            if (inst->getUses()[0]->getType()->isConst())
                            {
                                if (binary_expr.count(inst->getUses()[1]))
                                {
                                    switch (std::get<0>(binary_expr[inst->getUses()[1]]))
                                    {
                                    case BinaryInstruction::ADD:
                                    {
                                        // const1_ - (const21 + a22)
                                        if (std::get<1>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, inst->getDef(), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() - std::get<1>(binary_expr[inst->getUses()[1]])->getEntry()->getValue())), std::get<2>(binary_expr[inst->getUses()[1]])));
                                        }
                                        // const1_ - (a21 + const22)
                                        else if (std::get<2>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, inst->getDef(), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() - std::get<2>(binary_expr[inst->getUses()[1]])->getEntry()->getValue())), std::get<1>(binary_expr[inst->getUses()[1]])));
                                        }
                                        break;
                                    }
                                    case BinaryInstruction::SUB:
                                    {
                                        // const1_ - (const21 - a22)
                                        if (std::get<1>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() - std::get<1>(binary_expr[inst->getUses()[1]])->getEntry()->getValue())), std::get<2>(binary_expr[inst->getUses()[1]])));
                                        }
                                        // const1_ - (a21 - const22)
                                        else if (std::get<2>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, inst->getDef(), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() + std::get<2>(binary_expr[inst->getUses()[1]])->getEntry()->getValue())), std::get<1>(binary_expr[inst->getUses()[1]])));
                                        }
                                        break;
                                    }
                                    default:
                                        break;
                                    }
                                }
                            }
                            // ... - const2_
                            else if (inst->getUses()[1]->getType()->isConst())
                            {
                                if (binary_expr.count(inst->getUses()[0]))
                                {
                                    switch (std::get<0>(binary_expr[inst->getUses()[0]]))
                                    {
                                    case BinaryInstruction::ADD:
                                    {
                                        // (const11 + a12) - const2_
                                        if (std::get<1>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), std::get<2>(binary_expr[inst->getUses()[0]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), -inst->getUses()[1]->getEntry()->getValue() + std::get<1>(binary_expr[inst->getUses()[0]])->getEntry()->getValue()))));
                                        }
                                        // (a11 + const12) - const22
                                        else if (std::get<2>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), std::get<1>(binary_expr[inst->getUses()[0]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), -inst->getUses()[1]->getEntry()->getValue() + std::get<2>(binary_expr[inst->getUses()[0]])->getEntry()->getValue()))));
                                        }
                                        break;
                                    }
                                    case BinaryInstruction::SUB:
                                    {
                                        // (const11 - a12) - const2_
                                        if (std::get<1>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, inst->getDef(), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), -inst->getUses()[1]->getEntry()->getValue() + std::get<1>(binary_expr[inst->getUses()[0]])->getEntry()->getValue())), std::get<2>(binary_expr[inst->getUses()[0]])));
                                        }
                                        // (a11 - const12) - const2_
                                        else if (std::get<2>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::ADD, inst->getDef(), std::get<1>(binary_expr[inst->getUses()[0]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), -inst->getUses()[1]->getEntry()->getValue() - std::get<2>(binary_expr[inst->getUses()[0]])->getEntry()->getValue()))));
                                        }
                                        break;
                                    }
                                    default:
                                        break;
                                    }
                                }
                            }
                            // ... - ...
                            else
                            {
                                if (binary_expr.count(inst->getUses()[0]) && binary_expr.count(inst->getUses()[1]))
                                {
                                    // a*b - b*a、a+b - b+a
                                    if (equal(binary_expr[inst->getUses()[0]], binary_expr[inst->getUses()[1]]))
                                    {
                                        freeInsts.insert(inst);
                                        inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 0)));
                                    }
                                    // a*b - a*c
                                    else if (std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::MUL && std::get<0>(binary_expr[inst->getUses()[1]]) == BinaryInstruction::MUL)
                                    {
                                        if (inst->getUses()[0]->getUses().size() == 1 && inst->getUses()[1]->getUses().size() == 1)
                                        {
                                            Operand *commonFactor = nullptr, *op1 = nullptr, *op2 = nullptr;
                                            if (equal(std::get<1>(binary_expr[inst->getUses()[0]]), std::get<1>(binary_expr[inst->getUses()[1]])))
                                            {
                                                commonFactor = std::get<1>(binary_expr[inst->getUses()[0]]);
                                                op1 = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                op2 = std::get<2>(binary_expr[inst->getUses()[1]]);
                                            }
                                            else if (equal(std::get<1>(binary_expr[inst->getUses()[0]]), std::get<2>(binary_expr[inst->getUses()[1]])))
                                            {
                                                commonFactor = std::get<1>(binary_expr[inst->getUses()[0]]);
                                                op1 = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                op2 = std::get<1>(binary_expr[inst->getUses()[1]]);
                                            }
                                            else if (equal(std::get<2>(binary_expr[inst->getUses()[0]]), std::get<1>(binary_expr[inst->getUses()[1]])))
                                            {
                                                commonFactor = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                op1 = std::get<1>(binary_expr[inst->getUses()[0]]);
                                                op2 = std::get<2>(binary_expr[inst->getUses()[1]]);
                                            }
                                            else if (equal(std::get<2>(binary_expr[inst->getUses()[0]]), std::get<2>(binary_expr[inst->getUses()[1]])))
                                            {
                                                commonFactor = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                op1 = std::get<1>(binary_expr[inst->getUses()[0]]);
                                                op2 = std::get<1>(binary_expr[inst->getUses()[1]]);
                                            }
                                            if (commonFactor != nullptr)
                                            {
                                                freeInsts.insert(inst);
                                                auto temp = new Operand(new TemporarySymbolEntry(inst->getDef()->getType(), SymbolTable::getLabel()));
                                                bb->insertAfter(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), temp, commonFactor), inst);
                                                inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, temp, op1, op2));
                                            }
                                        }
                                    }
                                    // // (b - a) - (c - a) or (a +- b) - (a +- c)
                                    // else if (std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::ADD && std::get<0>(binary_expr[inst->getUses()[1]]) == BinaryInstruction::SUB &&
                                    //          (equal(std::get<2>(binary_expr[inst->getUses()[1]]), std::get<1>(binary_expr[inst->getUses()[0]])) || equal(std::get<2>(binary_expr[inst->getUses()[1]]), std::get<2>(binary_expr[inst->getUses()[0]]))))
                                    // {

                                    // }
                                }
                                else if (binary_expr.count(inst->getUses()[0]))
                                {
                                    // (a - b) - a
                                    if (std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::SUB && equal(std::get<1>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]))
                                    {
                                        freeInsts.insert(inst);
                                        inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, inst->getDef(), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 0)), std::get<2>(binary_expr[inst->getUses()[0]])));
                                    }
                                    // (a + b) - a
                                    else if (std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::ADD && (equal(std::get<1>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]) || equal(std::get<2>(binary_expr[inst->getUses()[0]]), inst->getUses()[1])))
                                    {
                                        freeInsts.insert(inst);
                                        if (equal(std::get<1>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]))
                                            inst->replaceAllUsesWith(std::get<2>(binary_expr[inst->getUses()[0]]));
                                        else
                                        {
                                            assert(equal(std::get<2>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]));
                                            inst->replaceAllUsesWith(std::get<1>(binary_expr[inst->getUses()[0]]));
                                        }
                                    }
                                }
                                else if (binary_expr.count(inst->getUses()[1]))
                                {
                                    // a - (a + b)
                                    if (std::get<0>(binary_expr[inst->getUses()[1]]) == BinaryInstruction::ADD && (equal(std::get<1>(binary_expr[inst->getUses()[1]]), inst->getUses()[0]) || equal(std::get<2>(binary_expr[inst->getUses()[1]]), inst->getUses()[0])))
                                    {
                                        freeInsts.insert(inst);
                                        Operand *substractor = nullptr;
                                        if (equal(std::get<1>(binary_expr[inst->getUses()[1]]), inst->getUses()[0]))
                                            substractor = std::get<2>(binary_expr[inst->getUses()[1]]);
                                        else
                                        {
                                            assert(equal(std::get<2>(binary_expr[inst->getUses()[1]]), inst->getUses()[0]));
                                            substractor = std::get<1>(binary_expr[inst->getUses()[1]]);
                                        }
                                        inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::SUB, inst->getDef(), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), 0)), substractor));
                                    }
                                    // a - (a - b)
                                    else if (std::get<0>(binary_expr[inst->getUses()[1]]) == BinaryInstruction::SUB && equal(std::get<1>(binary_expr[inst->getUses()[1]]), inst->getUses()[0]))
                                    {
                                        freeInsts.insert(inst);
                                        inst->replaceAllUsesWith(std::get<2>(binary_expr[inst->getUses()[1]]));
                                    }
                                }
                            }
                            break;
                        }
                        case BinaryInstruction::MUL:
                        {
                            // const1_ * ...
                            if (inst->getUses()[0]->getType()->isConst())
                            {
                                if (binary_expr.count(inst->getUses()[1]))
                                {
                                    switch (std::get<0>(binary_expr[inst->getUses()[1]]))
                                    {
                                    case BinaryInstruction::ADD:
                                    {
                                        // 为防止溢出不优化了
                                        // const1_ * (const21 + a22)
                                        // const1_ * (a21 + const22)
                                        break;
                                    }
                                    case BinaryInstruction::SUB:
                                    {
                                        // const1_ * (const21 - a22)
                                        // const1_ * (a21 - const22)
                                        break;
                                    }
                                    case BinaryInstruction::MUL:
                                    {
                                        // const1_ * (const21 * a22)
                                        if (std::get<1>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), std::get<2>(binary_expr[inst->getUses()[1]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() * std::get<1>(binary_expr[inst->getUses()[1]])->getEntry()->getValue()))));
                                        }
                                        // const1_ * (a21 * const22)
                                        else if (std::get<2>(binary_expr[inst->getUses()[1]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), std::get<1>(binary_expr[inst->getUses()[1]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[0]->getEntry()->getValue() * std::get<2>(binary_expr[inst->getUses()[1]])->getEntry()->getValue()))));
                                        }
                                        break;
                                    }
                                    default:
                                        break;
                                    }
                                }
                            }
                            // ... * const2_
                            else if (inst->getUses()[1]->getType()->isConst())
                            {
                                if (binary_expr.count(inst->getUses()[0]))
                                {
                                    switch (std::get<0>(binary_expr[inst->getUses()[0]]))
                                    {
                                    case BinaryInstruction::ADD:
                                    {
                                        // TODO：可能溢出
                                        {
                                            // (a11 + const12) * const2_
                                            if (std::get<2>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                            {
                                                if (inst->getUses()[0]->getUses().size() == 1)
                                                {
                                                    freeInsts.insert(inst);
                                                    auto temp_op = new Operand(new TemporarySymbolEntry(inst->getDef()->getType(), SymbolTable::getLabel()));
                                                    auto old_def = inst->getDef();
                                                    auto const1 = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                    auto const2 = inst->getUses()[1];
                                                    inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, temp_op, std::get<1>(binary_expr[inst->getUses()[0]]), const2));
                                                    bb->insertAfter(new BinaryInstruction(BinaryInstruction::ADD, old_def, temp_op, new Operand(new ConstantSymbolEntry(old_def->getType(), const2->getEntry()->getValue() * const1->getEntry()->getValue()))), inst);
                                                }
                                            }
                                        }
                                        break;
                                    }
                                    case BinaryInstruction::SUB:
                                    {
                                        // TODO：可能溢出
                                        {
                                            // (a11 - const12) * const2_
                                            if (std::get<2>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                            {
                                                if (inst->getUses()[0]->getUses().size() == 1)
                                                {
                                                    freeInsts.insert(inst);
                                                    auto temp_op = new Operand(new TemporarySymbolEntry(inst->getDef()->getType(), SymbolTable::getLabel()));
                                                    auto old_def = inst->getDef();
                                                    auto const1 = std::get<2>(binary_expr[inst->getUses()[0]]);
                                                    auto const2 = inst->getUses()[1];
                                                    inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, temp_op, std::get<1>(binary_expr[inst->getUses()[0]]), const2));
                                                    bb->insertAfter(new BinaryInstruction(BinaryInstruction::SUB, old_def, temp_op, new Operand(new ConstantSymbolEntry(old_def->getType(), const2->getEntry()->getValue() * const1->getEntry()->getValue()))), inst);
                                                }
                                            }
                                        }
                                        break;
                                    }
                                    case BinaryInstruction::MUL:
                                    {
                                        // (const11 * a12) * const2_
                                        if (std::get<1>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), std::get<2>(binary_expr[inst->getUses()[0]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[1]->getEntry()->getValue() * std::get<1>(binary_expr[inst->getUses()[0]])->getEntry()->getValue()))));
                                        }
                                        // (a11 * const12) * const2_
                                        else if (std::get<2>(binary_expr[inst->getUses()[0]])->getType()->isConst())
                                        {
                                            freeInsts.insert(inst);
                                            inst = inst->replaceWith(new BinaryInstruction(BinaryInstruction::MUL, inst->getDef(), std::get<1>(binary_expr[inst->getUses()[0]]), new Operand(new ConstantSymbolEntry(inst->getDef()->getType(), inst->getUses()[1]->getEntry()->getValue() * std::get<2>(binary_expr[inst->getUses()[0]])->getEntry()->getValue()))));
                                        }
                                        break;
                                    }
                                    default:
                                        break;
                                    }
                                }
                            }
                            // ... * ...
                            else
                            {
                                if (binary_expr.count(inst->getUses()[0]) && binary_expr.count(inst->getUses()[1]))
                                {
                                    ;
                                }
                                else if (binary_expr.count(inst->getUses()[0]))
                                {
                                    ;
                                }
                                else if (binary_expr.count(inst->getUses()[1]))
                                {
                                    ;
                                }
                            }
                            break;
                        }
                        case BinaryInstruction::DIV:
                        {
                            // (a * b) / b
                            if (binary_expr.count(inst->getUses()[0]) && std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::MUL &&
                                (equal(std::get<1>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]) || equal(std::get<2>(binary_expr[inst->getUses()[0]]), inst->getUses()[1])))
                            {
                                if (equal(std::get<1>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]))
                                {
                                    freeInsts.insert(inst);
                                    inst->replaceAllUsesWith(std::get<2>(binary_expr[inst->getUses()[0]]));
                                }
                                else
                                {
                                    assert(equal(std::get<2>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]));
                                    freeInsts.insert(inst);
                                    inst->replaceAllUsesWith(std::get<1>(binary_expr[inst->getUses()[0]]));
                                }
                            }
                            break;
                        }
                        case BinaryInstruction::MOD:
                        {
                            // (a % b) % b
                            if (binary_expr.count(inst->getUses()[0]) && std::get<0>(binary_expr[inst->getUses()[0]]) == BinaryInstruction::MOD &&
                                equal(std::get<2>(binary_expr[inst->getUses()[0]]), inst->getUses()[1]))
                            {
                                freeInsts.insert(inst);
                                inst->replaceAllUsesWith(std::get<1>(binary_expr[inst->getUses()[0]]));
                            }
                            break;
                        }
                        default:
                            break;
                        }
                    }

                    if (!freeInsts.count(inst) && inst->getDef()->getType()->isInt())
                    {
                        binary_expr[inst->getDef()] = std::make_tuple((int)inst->getOpcode(), inst->getUses()[0], inst->getUses()[1]);
                        // printExpr(binary_expr[inst->getDef()]);
                    }
                    if (freeInsts.count(inst))
                    {
                        binary_expr.erase(inst->getDef());
                    }
                }
            }

            if (!freeInsts.empty())
            {
                change = true;
                for (auto inst : freeInsts)
                {
                    delete inst;
                }
            }
        }
    } while (change);
}
