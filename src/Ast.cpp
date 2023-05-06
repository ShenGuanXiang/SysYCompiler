#include "Ast.h"
#include "SymbolTable.h"
#include "Unit.h"
#include "Instruction.h"
#include "IRBuilder.h"
#include "Type.h"
#include <string>
#include <assert.h>
#include <exception>

extern FILE *yyout;
int Node::counter = 0;
IRBuilder *Node::builder = nullptr;
static int height = 0;
static std::vector<WhileStmt *> whileStack;
static Operand *arrayAddr;
static Type *arrayType = nullptr;
static std::vector<int> d;
std::vector<int> cur_dim;
ArrayType *cur_type;
std::vector<ExprNode *> vec_val;

static void get_vec_val(InitNode *cur_node)
{
    if (cur_node->isLeaf() || cur_node->getself() != nullptr)
        vec_val.push_back(cur_node->getself());
    else
        for (auto l : cur_node->getleaves())
            get_vec_val(l);
}

static ArrayType *arrTypeLike(ArrayType *old)
{
    if (old->isIntArray())
    {
        if (old->isConst())
            return new ConstIntArrayType();
        else
            return new IntArrayType();
    }
    else
    {
        assert(old->isFloatArray());
        if (old->isConst())
            return new ConstFloatArrayType();
        else
            return new FloatArrayType();
    }
}

Node::Node()
{
    seq = counter++;
}

void Ast::output()
{
    fprintf(yyout, "CompUnit\n");
    if (root != nullptr)
        root->output(4);
}

Type *ExprNode::getType()
{
    if (symbolEntry->getType()->isPTR())
    {
        // TODO :
        return ((PointerType*)symbolEntry->getType())->getValType();
    }
    else
    {
        if (!is_array_ele)
            return symbolEntry->getType();
        else
            return dynamic_cast<ArrayType *>(symbolEntry->getType())->getElemType();
    }
}

double ExprNode::getValue()
{
    return symbolEntry->getValue();
}

void Id::output(int level)
{
    std::string name, type;
    int scope;
    assert(symbolEntry->isVariable());
    name = ((IdentifierSymbolEntry *)symbolEntry)->getName();
    type = symbolEntry->getType()->toStr();
    scope = dynamic_cast<IdentifierSymbolEntry *>(symbolEntry)->getScope();
    fprintf(yyout, "%*cId \t name: %s \t scope: %d \t type: %s\n", level, ' ',
            name.c_str(), scope, type.c_str());
    if (indices != nullptr)
        indices->output(level + 4);
}

void UnaryExpr::output(int level)
{
    std::string op_str, type;
    switch (op)
    {
    case PLUS:
        op_str = "plus";
        break;
    case MINUS:
        op_str = "minus";
        break;
    case NOT:
        op_str = "not";
        break;
    }
    type = symbolEntry->getType()->toStr();
    fprintf(yyout, "%*cUnaryExpr \t op: %s \t type: %s\n", level, ' ', op_str.c_str(),
            type.c_str());
    expr->output(level + 4);
}

void BinaryExpr::output(int level)
{
    std::string op_str, type;
    switch (op)
    {
    case ADD:
        op_str = "add";
        break;
    case SUB:
        op_str = "sub";
        break;
    case MUL:
        op_str = "mul";
        break;
    case DIV:
        op_str = "div";
        break;
    case MOD:
        op_str = "mod";
        break;
    case LESS:
        op_str = "less";
        break;
    case GREATER:
        op_str = "great";
        break;
    case LESSEQ:
        op_str = "lesseq";
        break;
    case GREATEREQ:
        op_str = "greateq";
        break;
    case EQ:
        op_str = "eq";
        break;
    case NEQ:
        op_str = "neq";
        break;
    case AND:
        op_str = "and";
        break;
    case OR:
        op_str = "or";
        break;
    }
    type = symbolEntry->getType()->toStr();
    fprintf(yyout, "%*cBinaryExpr \t op: %s \t type: %s\n", level, ' ', op_str.c_str(),
            type.c_str());
    expr1->output(level + 4);
    expr2->output(level + 4);
}

void Constant::output(int level)
{
    std::string type, value;
    type = symbolEntry->getType()->toStr();
    value = symbolEntry->toStr();
    fprintf(yyout, "%*cLiteral \t value: %s \t type: %s\n", level, ' ',
            value.c_str(), type.c_str());
}

void ImplicitCast::output(int level)
{
    std::string old_type, new_type;
    old_type = expr->getType()->toStr();
    new_type = symbolEntry->getType()->toStr();
    fprintf(yyout, "%*cImplicitCast \t  %s -> %s\n", level, ' ', old_type.c_str(), new_type.c_str());
    expr->output(level + 4);
}

void CompoundStmt::output(int level)
{
    fprintf(yyout, "%*cCompoundStmt\n", level, ' ');
    if (stmt != nullptr)
        stmt->output(level + 4);
}

StmtNode *CompoundStmt::getStmt()
{
    return stmt;
}

void SeqStmt::output(int level)
{
    fprintf(yyout, "%*cSequence\n", level, ' ');
    for (auto stmt : stmts)
        stmt->output(level + 4);
}

void SeqStmt::addChild(StmtNode *next)
{
    stmts.push_back(next);
}

void DeclStmt::output(int level)
{
    fprintf(yyout, "%*cDeclStmt\n", level, ' ');
    id->output(level + 4);
    if (expr != nullptr)
        expr->output(level + 4);
    if (next != nullptr)
        next->output(level);
}

void DeclStmt::setNext(DeclStmt *next)
{
    this->next = next;
}

DeclStmt *DeclStmt::getNext()
{
    return this->next;
}

void IfStmt::output(int level)
{
    fprintf(yyout, "%*cIfStmt\n", level, ' ');
    cond->output(level + 4);
    thenStmt->output(level + 4);
}

void IfElseStmt::output(int level)
{
    fprintf(yyout, "%*cIfElseStmt\n", level, ' ');
    cond->output(level + 4);
    thenStmt->output(level + 4);
    elseStmt->output(level + 4);
}

void ReturnStmt::output(int level)
{
    fprintf(yyout, "%*cReturnStmt\n", level, ' ');
    if (!retValue)
        fprintf(yyout, "%*cVOID\n", level + 4, ' ');
    else
        retValue->output(level + 4);
}

void AssignStmt::output(int level)
{
    fprintf(yyout, "%*cAssignStmt\n", level, ' ');
    lval->output(level + 4);
    expr->output(level + 4);
}

void ExprStmt::output(int level)
{
    fprintf(yyout, "%*cExprStmt\n", level, ' ');
    expr->output(level + 4);
}

void NullStmt::output(int level)
{
    fprintf(yyout, "%*cNullStmt\n", level, ' ');
}

void BreakStmt::output(int level)
{
    fprintf(yyout, "%*cBreakStmt\n", level, ' ');
}

void ContinueStmt::output(int level)
{
    fprintf(yyout, "%*cContinueStmt\n", level, ' ');
}

void WhileStmt::output(int level)
{
    fprintf(yyout, "%*cWhileStmt\n", level, ' ');
    cond->output(level + 4);
    stmt->output(level + 4);
}

void FuncCallNode::output(int level)
{
    std::string name, type;
    int scope;
    SymbolEntry *funcEntry = funcId->getSymPtr();
    name = funcEntry->toStr();
    type = funcEntry->getType()->toStr();
    scope = dynamic_cast<IdentifierSymbolEntry *>(funcEntry)->getScope();
    fprintf(yyout, "%*cFuncCallNode \t funcName: %s \t funcType: %s \t scope: %d\n",
            level, ' ', name.c_str(), type.c_str(), scope);
    if (params != nullptr)
        params->output(level + 4);
    else
        fprintf(yyout, "%*cFuncCallParamsNode \t nullptr\n", level + 4, ' ');
}

std::vector<Type *> FuncCallParamsNode::getParamsType()
{
    std::vector<Type *> ans;
    for (auto param : paramsList)
        ans.push_back(param->getType());
    return ans;
}

void FuncCallParamsNode::addChild(ExprNode *next)
{
    paramsList.push_back(next);
}

void FuncCallParamsNode::output(int level)
{
    fprintf(yyout, "%*cFuncCallParamsNode\n", level, ' ');
    for (auto param : paramsList)
        param->output(level + 4);
}

void FuncDefNode::output(int level)
{
    std::string name, type;
    name = se->toStr();
    type = se->getType()->toStr();
    fprintf(yyout, "%*cFuncDefNode \t function name: %s \t type: %s\n", level, ' ',
            name.c_str(), type.c_str());
    if (params != nullptr)
        params->output(level + 4);
    else
        fprintf(yyout, "%*cFuncDefParamsNode \t nullptr\n", level + 4, ' ');
    stmt->output(level + 4);
}

void FuncDefParamsNode::output(int level)
{
    fprintf(yyout, "%*cFuncDefParamsNode\n", level, ' ');
    for (auto param : paramsList)
        param->output(level + 4);
}

void FuncDefParamsNode::addChild(Id *next)
{
    dynamic_cast<IdentifierSymbolEntry *>(next->getSymPtr())->setParamNo(paramsList.size());
    paramsList.push_back(next);
}

std::vector<Type *> FuncDefParamsNode::getParamsType()
{
    std::vector<Type *> ans;
    for (auto param : paramsList)
        ans.push_back(param->getType());
    return ans;
}

void Node::backPatch(std::vector<Instruction *> &list, BasicBlock *bb)
{
    for (auto &inst : list)
    {
        if (inst->isCond())
            dynamic_cast<CondBrInstruction *>(inst)->setTrueBranch(bb);
        else if (inst->isUncond())
            dynamic_cast<UncondBrInstruction *>(inst)->setBranch(bb);
    }
}

std::vector<Instruction *> Node::merge(std::vector<Instruction *> &list1, std::vector<Instruction *> &list2)
{
    std::vector<Instruction *> res(list1);
    res.insert(res.end(), list2.begin(), list2.end());
    return res;
}

void Ast::genCode(Unit *unit)
{
    IRBuilder *builder = new IRBuilder(unit);
    Node::setIRBuilder(builder);
    assert(root != nullptr);
    root->genCode();
}

void Id::genCode()
{
    if (getType()->isConst() && !is_array) // 常量折叠
        return;
    BasicBlock *bb = builder->getInsertBB();
    Operand *addr = dynamic_cast<IdentifierSymbolEntry *>(symbolEntry)->getAddr();
    if (!is_array)
    {
        setDst(new Operand(new TemporarySymbolEntry(symbolEntry->getType(), SymbolTable::getLabel())));
        new LoadInstruction(dst, addr, bb);
    }
    else
    {
        bool isPtr = (symbolEntry->getType())->isPTR();
        if (!isPtr)
                cur_type = ((ArrayType *)getSymPtr()->getType());
        else
            cur_type = (ArrayType *)((PointerType *)symbolEntry->getType())->getValType();
        if (indices != nullptr)
        {
            Operand *tempSrc = addr;
            Operand *dst1;
            if (isPtr)
            {
                dst1 = new Operand(new TemporarySymbolEntry(((PointerType *)addr->getType())->getValType(), SymbolTable::getLabel()));
                new LoadInstruction(dst1, addr, bb);
                tempSrc = dst1;
            }
            std::vector<int> currr_dim;
            if (!isPtr)
                currr_dim = cur_type->fetch(); // if is params, it should be 0
            else
                currr_dim = ((ArrayType *)((PointerType *)getSymPtr()->getType())->getValType())->fetch();
            if (currr_dim.size() != indices->getExprList().size() && !isPtr)
                is_FP = true;
            
            std::vector<Operand*> idx_operend;
            if (!isPtr) 
                idx_operend.push_back(nullptr);
            bool be_first = true;
            for (auto idx : indices->getExprList()) {
                idx->genCode();
                if (!isPtr) currr_dim.erase(currr_dim.begin());
                else
                {
                    if (!be_first) {
                        currr_dim.erase(currr_dim.begin());
                    }
                    be_first = false;
                }
                idx_operend.push_back(idx->getOperand());
            }
            ArrayType *curr_type;
            PointerType* final_type;
            if (currr_dim.size()) {
                if (cur_type->isIntArray())
                {
                    if (cur_type->isConst())
                        curr_type = new ConstIntArrayType();
                    else
                        curr_type = new IntArrayType();
                }
                else
                {
                    if (cur_type->isConst())
                        curr_type = new ConstFloatArrayType();
                    else
                        curr_type = new FloatArrayType();
                }
                curr_type->SetDim(currr_dim);
                final_type = new PointerType(curr_type);
            }
            else {
                if (cur_type->getElemType()->isInt())
                    final_type = new PointerType(TypeSystem::intType);
                else
                    final_type = new PointerType(TypeSystem::floatType);
            }
            Operand *tempDst = new Operand(new TemporarySymbolEntry(final_type, SymbolTable::getLabel()));
            new GepInstruction(tempDst, tempSrc, idx_operend, bb);
            tempSrc = tempDst;
            if (is_FP)
            {
                tempDst = new Operand(new TemporarySymbolEntry(final_type, SymbolTable::getLabel()));
                new GepInstruction(tempDst, tempSrc, std::vector<Operand *>{nullptr, nullptr}, bb);
                tempSrc = tempDst;
            }
            if (is_left)
            {
                arrayAddr = tempSrc;
                dst = tempSrc;
                return;
            }
            if (!isPtr)
            {
                if (getType()->isInt())
                    dst1 = new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel()));
                else
                    dst1 = new Operand(new TemporarySymbolEntry(TypeSystem::floatType, SymbolTable::getLabel()));
            }
            else
            {
                if (final_type->getValType()->isAnyInt())
                    dst1 = new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel()));
                else
                    dst1 = new Operand(new TemporarySymbolEntry(TypeSystem::floatType, SymbolTable::getLabel()));
            }
            if (is_FP)
            {
                // dst = new Operand(new TemporarySymbolEntry(new PointerType(curr_type->getElemType()), ((TemporarySymbolEntry *)tempSrc->getEntry())->getLabel()));
                // dst = new Operand(new TemporarySymbolEntry(new PointerType(((ArrayType*)final_type->getValType())->getElemType()), ((TemporarySymbolEntry *)tempSrc->getEntry())->getLabel()));
                dst = tempSrc;
                dst->getEntry()->setType(new PointerType(((ArrayType*)final_type->getValType())->getElemType()));
                // assert(0);
                is_FP = false;
                return;
            }
            new LoadInstruction(dst1, tempSrc, bb);
            dst = dst1;
        }
        else
        {
            if (symbolEntry->getType()->isPTR())
            {
                ArrayType *curr_type;
                if (cur_type->isIntArray())
                {
                    if (cur_type->isConst())
                        curr_type = new ConstIntArrayType();
                    else
                        curr_type = new IntArrayType();
                }
                else
                {
                    if (cur_type->isConst())
                        curr_type = new ConstFloatArrayType();
                    else
                        curr_type = new FloatArrayType();
                }
                std::vector<int> currdim = cur_type->fetch();
                curr_type->SetDim(currdim);
                Operand *dst1 = new Operand(new TemporarySymbolEntry(new PointerType(cur_type), SymbolTable::getLabel()));
                new LoadInstruction(dst1, addr, bb);
                // Operand* idx = new Operand(new ConstantSymbolEntry(TypeSystem::constIntType, 0));
                // Operand *dst1 = new Operand(new TemporarySymbolEntry(new PointerType(cur_type->getElemType()), SymbolTable::getLabel()));
                // new GepInstruction(dst1, addr, idx, bb);
                dst = dst1;
            }
            else
            {
                std::vector<int> FunP(((ArrayType *)symbolEntry->getType())->fetch());
                Type *curr_type;
                cur_type = ((ArrayType *)getSymPtr()->getType());
                if (FunP.size() > 0)
                {
                    FunP.erase(FunP.begin());
                    if (FunP.size() != 0)
                    {
                        curr_type = arrTypeLike(cur_type);
                        ((ArrayType *)curr_type)->SetDim(FunP);
                    }
                    else
                        curr_type = cur_type->getElemType();
                }
                else
                    assert(0);
                Operand *idx = new Operand(new ConstantSymbolEntry(TypeSystem::constIntType, 0));
                dst = new Operand(new TemporarySymbolEntry(new PointerType(curr_type), SymbolTable::getLabel()));
                new GepInstruction(dst, addr, std::vector<Operand *>{nullptr, idx}, bb);
            }
        }
    }
}

void UnaryExpr::genCode()
{
    updateDst();
    BasicBlock *bb = builder->getInsertBB();
    Function *func = (bb == nullptr) ? nullptr : bb->getParent();
    if (getType()->isConst()) // 常量折叠
    {
        if (op == NOT)
            if (height > 0)
            {
                if (getValue())
                {
                    BasicBlock *trueBB = nullptr;
                    true_list.push_back(new UncondBrInstruction(trueBB, bb));
                }
                else
                {
                    BasicBlock *falseBB = nullptr;
                    false_list.push_back(new UncondBrInstruction(falseBB, bb));
                }
            }
        return;
    }
    Operand *src1, *src2;
    if (op == MINUS)
    {
        expr->genCode();
        src2 = expr->getOperand();
        src1 = new Operand(new ConstantSymbolEntry(Var2Const(src2->getType()), 0));
        new BinaryInstruction(BinaryInstruction::SUB, dst, src1, src2, bb);
    }
    else if (op == NOT)
    {
        height--;
        expr->genCode();
        height++;
        src1 = expr->getOperand();
        src2 = new Operand(new ConstantSymbolEntry(Var2Const(src1->getType()), 0));
        new CmpInstruction(CmpInstruction::E, dst, src1, src2, bb);
        if (expr->getType()->isBool())
        {
            true_list = expr->falseList();
            false_list = expr->trueList();
        }
        else if (height > 0)
        {
            BasicBlock *trueBB = nullptr, *falseBB = new BasicBlock(func), *endBB = nullptr;
            true_list.push_back(new CondBrInstruction(trueBB, falseBB, dst, bb));
            false_list.push_back(new UncondBrInstruction(endBB, falseBB));
        }
    }
}

void BinaryExpr::genCode()
{
    updateDst();
    BasicBlock *bb = builder->getInsertBB();
    Function *func = (bb == nullptr) ? nullptr : bb->getParent();
    if (getType()->isConst()) // 常量折叠
    {
        if (getType()->isConstBool())
            if (height > 0)
            {
                if (getValue())
                {
                    BasicBlock *trueBB = nullptr;
                    true_list.push_back(new UncondBrInstruction(trueBB, bb));
                }
                else
                {
                    BasicBlock *falseBB = nullptr;
                    false_list.push_back(new UncondBrInstruction(falseBB, bb));
                }
            }
        return;
    }
    if (op == AND)
    {
        expr1->genCode();
        if (!expr1->trueList().empty()) // 常量折叠
        {
            BasicBlock *trueBB = new BasicBlock(func); // if the result of lhs is true, jump to the trueBB.
            backPatch(expr1->trueList(), trueBB);
            builder->setInsertBB(trueBB); // set the insert point to the trueBB so that intructions generated by expr2 will be inserted into it.
            expr2->genCode();
            true_list = expr2->trueList();
            false_list = merge(expr1->falseList(), expr2->falseList());
        }
        else // expr1 = false
            false_list = expr1->falseList();
    }
    else if (op == OR)
    {
        expr1->genCode();
        if (!expr1->falseList().empty()) // 常量折叠
        {
            BasicBlock *falseBB = new BasicBlock(func); // if the result of lhs is false, jump to the falseBB.
            backPatch(expr1->falseList(), falseBB);
            builder->setInsertBB(falseBB); // set the insert point to the falseBB so that intructions generated by expr2 will be inserted into it.
            expr2->genCode();
            true_list = merge(expr1->trueList(), expr2->trueList());
            false_list = expr2->falseList();
        }
        else // expr1 = true
            true_list = expr1->trueList();
    }
    else
    {
        height--;
        expr1->genCode();
        expr2->genCode();
        height++;
        Operand *src1 = expr1->getOperand(), *src2 = expr2->getOperand();
        if (op >= LESS && op <= NEQ)
        {
            int opcode;
            switch (op)
            {
            case LESS:
                opcode = CmpInstruction::L;
                break;
            case LESSEQ:
                opcode = CmpInstruction::LE;
                break;
            case GREATER:
                opcode = CmpInstruction::G;
                break;
            case GREATEREQ:
                opcode = CmpInstruction::GE;
                break;
            case EQ:
                opcode = CmpInstruction::E;
                break;
            case NEQ:
                opcode = CmpInstruction::NE;
                break;
            }
            new CmpInstruction(opcode, dst, src1, src2, bb);
            if (height > 0)
            {
                BasicBlock *trueBB = nullptr, *falseBB = new BasicBlock(func), *endBB = nullptr;
                true_list.push_back(new CondBrInstruction(trueBB, falseBB, dst, bb));
                false_list.push_back(new UncondBrInstruction(endBB, falseBB));
            }
        }
        else if (op >= ADD && op <= MOD)
        {
            int opcode;
            switch (op)
            {
            case ADD:
                opcode = BinaryInstruction::ADD;
                break;
            case SUB:
                opcode = BinaryInstruction::SUB;
                break;
            case MUL:
                opcode = BinaryInstruction::MUL;
                break;
            case DIV:
                opcode = BinaryInstruction::DIV;
                break;
            case MOD:
                opcode = BinaryInstruction::MOD;
                break;
            }
            new BinaryInstruction(opcode, dst, src1, src2, bb);
        }
    }
}

void Constant::genCode()
{
    BasicBlock *bb = builder->getInsertBB();
    if (getType() == TypeSystem::constBoolType)
        if (height > 0)
        {
            if (getValue())
            {
                BasicBlock *trueBB = nullptr;
                true_list.push_back(new UncondBrInstruction(trueBB, bb));
            }
            else
            {
                BasicBlock *falseBB = nullptr;
                false_list.push_back(new UncondBrInstruction(falseBB, bb));
            }
        }
    return;
}

void ImplicitCast::genCode()
{
    updateDst();
    BasicBlock *bb = builder->getInsertBB();
    Function *func = (bb == nullptr) ? nullptr : bb->getParent();
    if (getType()->isConst()) // 常量折叠
    {
        if (getType() == TypeSystem::constBoolType)
            if (height > 0)
            {
                if (getValue())
                {
                    BasicBlock *trueBB = nullptr;
                    true_list.push_back(new UncondBrInstruction(trueBB, bb));
                }
                else
                {
                    BasicBlock *falseBB = nullptr;
                    false_list.push_back(new UncondBrInstruction(falseBB, bb));
                }
            }
        return;
    }
    expr->genCode();
    // int/float -> bool
    if (this->getType()->isBool() || this->getType()->isConstBool())
    {
        Operand *src1 = expr->getOperand(), *src2 = new Operand(new ConstantSymbolEntry(Var2Const(src1->getType()), 0));
        new CmpInstruction(CmpInstruction::NE, dst, src1, src2, bb);
        if (height > 0)
        {
            BasicBlock *trueBB = nullptr, *falseBB = new BasicBlock(func), *endBB = nullptr;
            true_list.push_back(new CondBrInstruction(trueBB, falseBB, dst, bb));
            false_list.push_back(new UncondBrInstruction(endBB, falseBB));
        }
    }
    else if (this->getType()->isInt() || this->getType()->isConstInt())
    {
        Operand *src = expr->getOperand();
        // bool -> int
        if (src->getType()->isBool() || src->getType()->isConstBool())
            new ZextInstruction(dst, src, bb);
        // float -> int
        else
        {
            assert(src->getType()->isFloat() || src->getType()->isConstFloat());
            new IntFloatCastInstruction(IntFloatCastInstruction::F2S, dst, src, bb);
        }
    }
    else
    {
        assert(this->getType()->isFloat() || this->getType()->isConstFloat());
        Operand *src = expr->getOperand();
        // bool -> float
        if (src->getType()->isBool() || src->getType()->isConstBool())
        {
            Type *type = src->getType()->isConstBool() ? TypeSystem::constIntType : TypeSystem::intType;
            Operand *t = new Operand(new TemporarySymbolEntry(type, SymbolTable::getLabel()));
            new ZextInstruction(t, src, bb);
            new IntFloatCastInstruction(IntFloatCastInstruction::S2F, dst, t, bb);
        }
        // int -> float
        else
        {
            assert(src->getType() == TypeSystem::intType || src->getType() == TypeSystem::constIntType);
            new IntFloatCastInstruction(IntFloatCastInstruction::S2F, dst, src, bb);
        }
    }
}

void CompoundStmt::genCode()
{
    if (stmt != nullptr)
        stmt->genCode();
}

void SeqStmt::genCode()
{
    for (auto stmt : stmts)
        stmt->genCode();
}

void InitNode::genCode(int level)
{
    for (size_t i = 0; i < vec_val.size(); i++)
    {
        int pos = i;
        ArrayType *curr_type = arrTypeLike(cur_type);
        std::vector<int> curr_dim(cur_dim);
        Operand *addr = arrayAddr;
        std::vector<Operand*> idx_operand{nullptr};
        for (size_t j = 0; j < d.size(); j++)
        {
            curr_dim.erase(curr_dim.begin());
            idx_operand.push_back(new Operand(new ConstantSymbolEntry(TypeSystem::constIntType, pos / d[j])));
            fprintf(stderr, "currdim is %s\n", addr->getType()->toStr().c_str());
            pos %= d[j];
        }
        curr_type->SetDim(curr_dim);
        Operand* final_offset = new Operand(new TemporarySymbolEntry(new PointerType(curr_type), SymbolTable::getLabel()));
        vec_val[i]->genCode();
        Operand *src = vec_val[i]->getOperand();
        new GepInstruction(final_offset, addr, idx_operand, builder->getInsertBB());
        new StoreInstruction(final_offset, src, builder->getInsertBB());
        curr_dim.clear();
        // assert(cur_dim.empty());
    }
}

void IndicesNode::output(int level)
{
    fprintf(yyout, "%*cIndicesNode\n", level, ' ');
    for (auto expr : exprList)
    {
        expr->output(level + 4);
    }
}

void InitNode::output(int level)
{
    std::string constStr = isconst ? "true" : "false";
    fprintf(yyout, "%*cInitValNode\tisConst:%s\n", level, ' ', constStr.c_str());
    for (auto l : leaves)
        l->output(level + 4);
    if (leaf != nullptr)
        leaf->output(level + 4);
}

void IndicesNode::genCode()
{
    for (auto ele : exprList)
        ele->genCode();
}

DeclStmt::DeclStmt(Id *id, InitNode *expr, bool isConst, bool isArray) : id(id), expr(expr), BeConst(isConst), BeArray(isArray)
{
    next = nullptr;

    if (expr != nullptr)
    {
        if (id->getType()->isARRAY())
        {
            std::vector<int> origin_dim = ((ArrayType *)(id->getType()))->fetch();
            expr->fill(0, origin_dim, ((ArrayType *)(id->getType()))->getElemType());
            vec_val.clear();
            get_vec_val(expr);
            std::vector<double> arrVals;
            for (auto elem : vec_val)
                arrVals.push_back(elem->getValue());
            id->getSymPtr()->setArrVals(arrVals);
        }
    }

    this->head = nullptr;
};

void DeclStmt::genCode()
{
    if (id->getType()->isConst() && !id->getSymPtr()->getType()->isARRAY()) // 常量折叠
        return;
    Operand *addr;
    IdentifierSymbolEntry *se = dynamic_cast<IdentifierSymbolEntry *>(id->getSymPtr());
    if (se->isGlobal())
    {
        SymbolEntry *addr_se = new IdentifierSymbolEntry(*se);
        addr_se->setType(new PointerType(se->getType()));
        addr = new Operand(addr_se);
        se->setAddr(addr);
        this->builder->getUnit()->insertDecl(se);
    }
    else if (se->isLocal())
    {
        Function *func = builder->getInsertBB()->getParent();
        BasicBlock *entry = func->getEntry();
        addr = new Operand(new TemporarySymbolEntry(new PointerType(se->getType()), SymbolTable::getLabel()));
        Instruction *alloca = new AllocaInstruction(addr, se); // allocate space for local id in function stack.
        entry->insertFront(alloca);                            // allocate instructions should be inserted into the begin of the entry block.
        se->setAddr(addr);                                     // set the addr operand in symbol entry so that we can use it in subsequent code generation.
        Operand *temp = nullptr;
        if (se->isParam())
            temp = se->getAddr();
        if (expr != nullptr)
        {
            BasicBlock *bb = builder->getInsertBB();
            assert(addr != nullptr);
            // a[k1][k2][,,,][kn] on a[n1][n2][...][nn], off = k1 * n2 * ... * nn + k2 * n3 * .... * nn + kn * 1
            // off = kn * b1 + kn-1 * b2 + ... + k1 * bn, bn = bn - 1 nn-1
            if (se->getType()->isARRAY())
            {
                arrayType = se->getType();
                cur_type = arrTypeLike((ArrayType *)arrayType);
                vec_val.clear();
                get_vec_val(expr);
                cur_dim = ((ArrayType *)arrayType)->fetch();
                d = ((ArrayType *)arrayType)->fetch();
                d.push_back(1), d.erase(d.begin());
                for (int i = d.size() - 2; i >= 0; i--)
                    d[i] = d[i + 1] * d[i];
                arrayAddr = addr;
                expr->genCode(0);
            }
            else
            {
                expr->getself()->genCode();
                Operand *addr = dynamic_cast<IdentifierSymbolEntry *>(se)->getAddr();
                Operand *src = expr->getself()->getOperand();
                if (!se->isGlobal()) // bb = nullptr
                    new StoreInstruction(addr, src, bb);
            }
        }
        if (se->isParam())
        {
            BasicBlock *bb = builder->getInsertBB();
            new StoreInstruction(addr, temp, bb);
        }
    }
    if (next != nullptr)
        next->genCode();
}

void IfStmt::genCode()
{
    Function *func = builder->getInsertBB()->getParent();
    BasicBlock *then_bb, *end_bb = new BasicBlock(func);

    height = 1;
    cond->genCode();
    height = 0;

    if (!cond->trueList().empty()) // 常量折叠，排除if(0)
    {
        then_bb = new BasicBlock(func);
        backPatch(cond->trueList(), then_bb);
        builder->setInsertBB(then_bb);
        thenStmt->genCode();
        then_bb = builder->getInsertBB();
        new UncondBrInstruction(end_bb, then_bb);
    }

    backPatch(cond->falseList(), end_bb);
    builder->setInsertBB(end_bb);
}

void IfElseStmt::genCode()
{
    Function *func = builder->getInsertBB()->getParent();
    BasicBlock *then_bb, *else_bb, *end_bb = new BasicBlock(func);

    height = 1;
    cond->genCode();
    height = 0;

    if (!cond->trueList().empty()) // 常量折叠，排除if(0)
    {
        then_bb = new BasicBlock(func);
        backPatch(cond->trueList(), then_bb);
        builder->setInsertBB(then_bb);
        thenStmt->genCode();
        then_bb = builder->getInsertBB();
        new UncondBrInstruction(end_bb, then_bb);
    }

    if (!cond->falseList().empty()) // 常量折叠，排除if(1)
    {
        else_bb = new BasicBlock(func);
        backPatch(cond->falseList(), else_bb);
        builder->setInsertBB(else_bb);
        elseStmt->genCode();
        else_bb = builder->getInsertBB();
        new UncondBrInstruction(end_bb, else_bb);
    }

    builder->setInsertBB(end_bb);
}

void ReturnStmt::genCode()
{
    BasicBlock *bb = builder->getInsertBB();
    if (retValue == nullptr)
        new RetInstruction(nullptr, bb);
    else
    {
        if (retValue->getSymPtr()->getType()->isARRAY())
            cur_type = (ArrayType *)(retValue->getSymPtr()->getType());
        retValue->genCode();
        new RetInstruction(retValue->getOperand(), bb);
    }
}

void AssignStmt::genCode()
{
    if (lval->getType()->isConst()) // 常量不会被重新赋值，这里的折叠应该没有作用
        return;
    BasicBlock *bb = builder->getInsertBB();
    expr->genCode();
    if (lval->getSymPtr()->getType()->isARRAY() || lval->getSymPtr()->getType()->isPTR())
    {
        cur_type = (ArrayType *)(lval->getSymPtr()->getType());
        lval->genCode();
        Operand *addr = arrayAddr;
        Operand *src = expr->getOperand();
        new StoreInstruction(addr, src, bb);
    }
    else
    {
        Operand *addr = dynamic_cast<IdentifierSymbolEntry *>(lval->getSymPtr())->getAddr();
        Operand *src = expr->getOperand();
        /***
         * We haven't implemented array yet, the lval can only be ID. So we just store the result of the `expr` to the addr of the id.
         * If you want to implement array, you have to caculate the address first and then store the result into it.
         */
        new StoreInstruction(addr, src, bb);
    }
}

void ExprStmt::genCode()
{
    expr->genCode();
}

void NullStmt::genCode()
{
}

void BreakStmt::genCode()
{
    // assert(whileStack.size() != 0 && "Break not in While loop!");
    Function *func = builder->getInsertBB()->getParent();
    BasicBlock *bb = builder->getInsertBB();
    // 首先获取当前所在的while
    WhileStmt *whileStmt = (*whileStack.rbegin());
    // 获取结束block
    BasicBlock *end_bb = whileStmt->getEndBlock();
    // 在当前基本块中生成一条跳转到结束的语句
    new UncondBrInstruction(end_bb, bb);
    // 声明一个新的基本块用来插入后续的指令
    BasicBlock *nextBlock = new BasicBlock(func);
    builder->setInsertBB(nextBlock);
}

void ContinueStmt::genCode()
{
    // assert(whileStack.size() != 0 && "Continue not in While loop!");
    Function *func = builder->getInsertBB()->getParent();
    BasicBlock *bb = builder->getInsertBB();
    // 首先获取当前所在的while
    WhileStmt *whileStmt = (*whileStack.rbegin());
    // 获取条件判断block
    BasicBlock *cond_bb = whileStmt->getCondBlock();
    // 在当前基本块中生成一条跳转到条件判断的语句
    new UncondBrInstruction(cond_bb, bb);
    // 声明一个新的基本块用来插入后续的指令
    BasicBlock *nextBlock = new BasicBlock(func);
    builder->setInsertBB(nextBlock);
}

void WhileStmt::genCode()
{
    whileStack.push_back(this);
    Function *func = builder->getInsertBB()->getParent();
    BasicBlock *stmt_bb, *cond_bb, *end_bb, *bb = builder->getInsertBB();
    cond_bb = new BasicBlock(func);
    end_bb = new BasicBlock(func);

    condb = cond_bb;
    endb = end_bb;
    // 先从当前的bb跳转到cond_bb进行条件判断
    new UncondBrInstruction(cond_bb, bb);

    // 调整插入点到cond_bb，对条件判断部分生成中间代码
    builder->setInsertBB(cond_bb);
    height = 1;
    cond->genCode();
    height = 0;
    backPatch(cond->falseList(), end_bb);

    if (!cond->trueList().empty()) // 常量折叠，排除while(0)
    {                              // 调整插入点到stmt_bb，对循环体部分生成中间代码
        stmt_bb = new BasicBlock(func);
        backPatch(cond->trueList(), stmt_bb);
        builder->setInsertBB(stmt_bb);
        stmt->genCode(); // 当前bb可能发生变化

        // 循环倒置：循环体完成之后，增加条件判断部分的中间代码跳转回原始stmt_bb
        height = 1;
        cond->genCode();
        height = 0;
        backPatch(cond->falseList(), end_bb);
        backPatch(cond->trueList(), stmt_bb);
    }

    // 重新调整插入点到end_bb
    builder->setInsertBB(end_bb);

    // 将当前的whileStmt出栈
    whileStack.pop_back();
}

void FuncCallParamsNode::genCode()
{
    for (auto it : paramsList)
        it->genCode();
}

void FuncCallNode::genCode()
{
    updateDst();
    IdentifierSymbolEntry *se = dynamic_cast<IdentifierSymbolEntry *>(funcId->getSymPtr());
    if (se->isLibFunc())
        builder->getUnit()->insertDecl(se);
    BasicBlock *bb = builder->getInsertBB();
    if (params == nullptr)
    {
        std::vector<Operand *> emptyList;
        new FuncCallInstruction(dst, emptyList, se, bb);
    }
    else
    {
        params->genCode();
        std::vector<Operand *> Oplist;
        for (auto it : params->getParams())
            Oplist.push_back(it->getOperand());
        new FuncCallInstruction(dst, Oplist, se, bb);
    }
}

void FuncDefParamsNode::genCode()
{
    Function *func = builder->getInsertBB()->getParent();
    BasicBlock *entry = func->getEntry();
    for (auto it : paramsList)
    {
        IdentifierSymbolEntry *se = dynamic_cast<IdentifierSymbolEntry *>(it->getSymPtr());
        Type *type;
        if (it->getIndices() != nullptr)
        {
            std::vector<int> dimensions;
            for (auto idx : it->getIndices()->getExprList())
                dimensions.push_back(idx->getSymPtr()->getValue());
            dimensions.erase(dimensions.begin());
            ArrayType *new_type = arrTypeLike((ArrayType *)(it->getSymPtr()->getType()));
            new_type->SetDim(dimensions);
            type = new PointerType(new_type);
            it->getSymPtr()->setType(type);
        }
        else
            type = new PointerType(it->getSymPtr()->getType());
        func->insertParams(it->getOperand()->getEntry());
        SymbolEntry *addr_se;
        if (it->getIndices() != nullptr)
            addr_se = new TemporarySymbolEntry(new PointerType(type), SymbolTable::getLabel());
        else
            addr_se = new TemporarySymbolEntry(type, SymbolTable::getLabel());
        Operand *addr = new Operand(addr_se);
        Instruction *alloca = new AllocaInstruction(addr, se); // allocate space for local id in function stack.
        entry->insertFront(alloca);                            // allocate instructions should be inserted into the begin of the entry block.
        se->setAddr(addr);
        Operand *src = it->getOperand();
        func->paramOp.push_back(src);
        /***
         * We haven't implemented array yet, the lval can only be ID. So we just store the result of the `expr` to the addr of the id.
         * If you want to implement array, you have to caculate the address first and then store the result into it.
         */
        new StoreInstruction(addr, src, entry);
        dynamic_cast<IdentifierSymbolEntry *>(it->getSymPtr())->setParamOpe(src);
    }
}

void FuncDefNode::genCode()
{
    Unit *unit = builder->getUnit();
    Function *func = new Function(unit, se);
    BasicBlock *entry = func->getEntry();
    // set the insert point to the entry basicblock of this function.
    builder->setInsertBB(entry);
    if (params != nullptr)
        params->genCode();
    stmt->genCode();
    /**
     * Construct control flow graph. You need do set succs and preds for each basic block.
     */
    for (auto bb = func->begin(); bb != func->end(); bb++)
    {
        // 清除ret之后的全部指令
        Instruction *index = (*bb)->begin();
        while (index != (*bb)->end())
        {
            if (index->isRet())
            {
                while (index != (*bb)->rbegin())
                    delete (index->getNext());
                break;
            }
            index = index->getNext();
        }
        // 获取该块的最后一条指令
        Instruction *last = (*bb)->rbegin();
        // (*bb)->output();
        // 对于有条件的跳转指令，需要对其true分支和false分支都设置控制流关系
        if (last->isCond())
        {
            BasicBlock *trueBB = dynamic_cast<CondBrInstruction *>(last)->getTrueBranch();
            BasicBlock *falseBB = dynamic_cast<CondBrInstruction *>(last)->getFalseBranch();
            (*bb)->addSucc(trueBB);
            (*bb)->addSucc(falseBB);
            trueBB->addPred(*bb);
            falseBB->addPred(*bb);
        }
        // 对于无条件的跳转指令，只需要对其目标基本块设置控制流关系即可
        else if (last->isUncond())
        {
            BasicBlock *dstBB = dynamic_cast<UncondBrInstruction *>(last)->getBranch();
            (*bb)->addSucc(dstBB);
            dstBB->addPred(*bb);
        }
        // 填充ret void
        else
        {
            if (!last->isRet() /*&& dynamic_cast<FunctionType *>(se->getType())->getRetType()->isVoid()*/)
                new RetInstruction(nullptr, *bb);
        }
    }
}

// void Ast::typeCheck()
// {
//     if (root != nullptr)
//         root->typeCheck();
// }

// void FuncDefNode::typeCheck()
// {
// }

// void BinaryExpr::typeCheck()
// {
// }

// void Constant::typeCheck()
// {
// }

// void Id::typeCheck()
// {
// }

// void IfStmt::typeCheck()
// {
// }

// void IfElseStmt::typeCheck()
// {
// }

// void CompoundStmt::typeCheck()
// {
// }

// void SeqStmt::typeCheck()
// {
// }

// void DeclStmt::typeCheck()
// {
// }

// void ReturnStmt::typeCheck()
// {
// }

// void AssignStmt::typeCheck()
// {
// }

ExprNode *typeCast(ExprNode *fromNode, Type *to)
{
    Type *from = fromNode->getType();
    assert(convertible(from, to));
    if (from->isARRAY() && to->isARRAY())
        return fromNode;
    if (from != to && !(from == TypeSystem::constIntType && to == TypeSystem::intType) &&
        !(from == TypeSystem::constFloatType && to == TypeSystem::floatType) &&
        !(from == TypeSystem::constBoolType && to == TypeSystem::boolType)) // need cast
    {
        if (fromNode->getSymPtr()->getType()->isConst()) // 常量的转换不再占用隐式转换ast节点
        {
            double val;
            if (to == TypeSystem::boolType || to == TypeSystem::constBoolType)
                val = static_cast<bool>(fromNode->getValue());
            else if (to == TypeSystem::intType || to == TypeSystem::constIntType)
                val = static_cast<int>(fromNode->getValue());
            else
            {
                assert(to == TypeSystem::floatType || to == TypeSystem::constFloatType);
                val = static_cast<double>(fromNode->getValue());
            }
            // 字面常量
            if (fromNode->getSymPtr()->isConstant())
                ;
            // 常变量
            else
            {
                assert(fromNode->getSymPtr()->isVariable());
                auto newSymPtr = new IdentifierSymbolEntry(*(IdentifierSymbolEntry *)(fromNode->getSymPtr()));
                fromNode->setSymPtr(newSymPtr);
                fromNode->setDst(new Operand(newSymPtr));
            }
            fromNode->getSymPtr()->setType(Var2Const(to));
            fromNode->getSymPtr()->setValue(val);
            return fromNode;
        }
        // 类型检查2：隐式转换
        fprintf(stderr, "implicit type conversion: %s -> %s\n", from->toStr().c_str(), to->toStr().c_str());
        SymbolEntry *se;
        if (to->isConst()) // 常量传播
        {
            double val;
            if (to == TypeSystem::constBoolType)
                val = static_cast<bool>(fromNode->getValue());
            else if (to == TypeSystem::constIntType)
                val = static_cast<int>(fromNode->getValue());
            else
            {
                assert(to == TypeSystem::constFloatType);
                val = static_cast<double>(fromNode->getValue());
            }
            se = new ConstantSymbolEntry(to, val);
        }
        else
            se = new TemporarySymbolEntry(to, SymbolTable::getLabel());
        return new ImplicitCast(se, fromNode);
    }
    else
        return fromNode;
}

void InitNode::fill(int level, std::vector<int> d, Type *type)
{
    if (level == (int)d.size() || leaf != nullptr)
    {
        if (leaf == nullptr)
            setleaf(new Constant(new ConstantSymbolEntry(Var2Const(type), 0)));
        return;
    }
    int cap = 1, num = 0;
    for (size_t i = level + 1; i < d.size(); i++)
        cap *= d[i];
    for (int i = (int)leaves.size() - 1; i >= 0; i--)
        if (leaves[i]->isLeaf())
            num++;
        else
            break;
    while (num % cap)
    {
        InitNode *new_const_node = new InitNode(true);
        new_const_node->setleaf(new Constant(new ConstantSymbolEntry(Var2Const(type), 0)));
        addleaf(new_const_node);
        num++;
    }
    int t = getSize(cap);
    while (t < d[level])
    {
        InitNode *new_node = new InitNode(true);
        addleaf(new_node);
        t++;
    }
    for (auto l : leaves)
        l->fill(level + 1, d, type);
}

int InitNode::getSize(int d_nxt)
{
    int num = 0, cur_fit = 0;
    for (auto l : leaves)
    {
        if (l->leaf != nullptr)
            num++;
        else
            cur_fit++;
        if (num == d_nxt)
        {
            cur_fit++;
            num = 0;
        }
    }
    return cur_fit + num / d_nxt;
}
