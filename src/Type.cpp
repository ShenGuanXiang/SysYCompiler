#include "Type.h"
#include <sstream>
#include <assert.h>

IntType TypeSystem::commonInt = IntType(32);
FloatType TypeSystem::commonFloat = FloatType(32);
IntType TypeSystem::commonBool = IntType(1);
VoidType TypeSystem::commonVoid = VoidType();
ConstIntType TypeSystem::commonConstInt = ConstIntType(32);
ConstFloatType TypeSystem::commonConstFloat = ConstFloatType(32);
ConstIntType TypeSystem::commonConstBool = ConstIntType(1);

Type *TypeSystem::intType = &commonInt;
Type *TypeSystem::floatType = &commonFloat;
Type *TypeSystem::boolType = &commonBool;
Type *TypeSystem::voidType = &commonVoid;
Type *TypeSystem::constIntType = &commonConstInt;
Type *TypeSystem::constFloatType = &commonConstFloat;
Type *TypeSystem::constBoolType = &commonConstBool;

static std::vector<Type *> newTypes; // 用来回收new出来的Type

FunctionType::FunctionType(Type *returnType, std::vector<Type *> paramsType) : Type(Type::FUNC), returnType(returnType), paramsType(paramsType)
{
    newTypes.push_back(this);
}

PointerType::PointerType(Type *valueType) : Type(Type::PTR), valueType(valueType)
{
    size = 32;
    newTypes.push_back(this);
};

ArrayType::ArrayType(int eleType) : Type(eleType)
{
    size = 32;
    newTypes.push_back(this);
}

Type *ArrayType::getElemType()
{
    return isConstIntArray()     ? TypeSystem::constIntType
           : isConstFloatArray() ? TypeSystem::constFloatType
           : isIntArray()        ? TypeSystem::intType
                                 : TypeSystem::floatType;
}

// to do : toStr 方法还需要修改

std::string IntType::toStr()
{
    std::ostringstream buffer;
    buffer << "i" << size;
    return buffer.str();
}

std::string FloatType::toStr()
{
    return "float";
    // return "double";
}

std::string VoidType::toStr()
{
    return "void";
}

std::string ConstIntType::toStr()
{
    std::ostringstream buffer;
    buffer << "i" << size;
    return buffer.str();
}

std::string ConstFloatType::toStr()
{
    return "float";
    // return "double";
}

std::string ArrayType::toStr()
{
    Type *type = getElemType();
    int count = 0;
    bool flag = false;
    std::vector<std::string> vec;
    for (auto d : dim)
    {
        std::ostringstream buffer;
        if (d == -1)
            flag = true;
        else
        {
            buffer << "[" << d << " x ";
            count++;
            vec.push_back(buffer.str());
        }
    }
    std::ostringstream buffer;
    for (auto it = vec.begin(); it != vec.end(); it++)
        buffer << *it;
    buffer << type->toStr();
    while (count--)
        buffer << ']';
    if (flag)
        buffer << '*';
    return buffer.str();
}

std::string FunctionType::toStr()
{
    std::ostringstream buffer;
    buffer << returnType->toStr() << "(";
    for (size_t i = 0; i != paramsType.size(); i++)
    {
        if (i)
            buffer << ", ";
        buffer << paramsType[i]->toStr();
    }
    buffer << ")";
    return buffer.str();
}

std::string PointerType::toStr()
{
    std::ostringstream buffer;
    buffer << valueType->toStr() << "*";
    return buffer.str();
}

bool isIllegalOpType(Type *type)
{
    // to do : array type
    bool ret = (!type->isInt()) && (!type->isConstInt()) && (!type->isFloat()) && (!type->isConstFloat());
    // 类型检查3：不合理运算数类型
    if (ret)
        fprintf(stderr, "illegal operand type: %s\n", type->toStr().c_str());
    return ret;
}

Type *unaryMax(Type *type, unsigned int opcode)
{
    // to do : consider array type
    assert(!isIllegalOpType(type));
    if (opcode == TypeSystem::NOT)
        return type->isConst() ? TypeSystem::constBoolType : TypeSystem::boolType;
    assert((opcode == TypeSystem::MINUS) || (opcode == TypeSystem::PLUS));
    assert(type->isInt() || type->isFloat());
    return type->isConstBool() ? TypeSystem::constIntType : type->isBool() ? TypeSystem::intType
                                                                           : type;
}

Type *calcMax(Type *type1, Type *type2)
{
    // to do : consider array type
    assert(!isIllegalOpType(type1));
    assert(!isIllegalOpType(type2));
    if (type1 == TypeSystem::floatType || type2 == TypeSystem::floatType ||
        (type1 == TypeSystem::constFloatType && (type2 == TypeSystem::intType || type2 == TypeSystem::boolType)) ||
        (type2 == TypeSystem::constFloatType && (type1 == TypeSystem::intType || type1 == TypeSystem::boolType)))
        return TypeSystem::floatType;
    else if (type1 == TypeSystem::constFloatType || type2 == TypeSystem::constFloatType)
        return TypeSystem::constFloatType;
    else if ((type1 == TypeSystem::intType || type1 == TypeSystem::boolType) ||
             (type2 == TypeSystem::intType || type2 == TypeSystem::boolType))
        return TypeSystem::intType;
    assert(type1->isConstInt() && type2->isConstInt());
    return TypeSystem::constIntType;
}

Type *relMax(Type *type1, Type *type2)
{
    if (type1 == TypeSystem::constBoolType && type2 == TypeSystem::constBoolType)
        return TypeSystem::constBoolType;
    else if ((type1 == TypeSystem::boolType || type1 == TypeSystem::constBoolType) && (type2 == TypeSystem::boolType || type2 == TypeSystem::constBoolType))
        return TypeSystem::boolType;
    else
        return calcMax(type1, type2);
}

Type *logicMax(Type *type1, Type *type2)
{
    // to do : consider array type
    assert(!isIllegalOpType(type1));
    assert(!isIllegalOpType(type2));
    if (type1 == TypeSystem::intType || type1 == TypeSystem::boolType || type1 == TypeSystem::floatType ||
        type2 == TypeSystem::intType || type2 == TypeSystem::boolType || type2 == TypeSystem::floatType)
        return TypeSystem::boolType;
    assert((type1->isConstInt() || type1->isConstFloat()) && (type2->isConstInt() || type2->isConstFloat()));
    return TypeSystem::constBoolType;
}

Type *Var2Const(Type *type)
{
    if (type == TypeSystem::intType)
        return TypeSystem::constIntType;
    if (type == TypeSystem::boolType)
        return TypeSystem::constBoolType;
    if (type == TypeSystem::floatType)
        return TypeSystem::constFloatType;

    // to do : array type
    assert(type->isConstInt() || type->isConstFloat());
    return type;
}

bool convertible(Type *from, Type *to)
{
    // to do : array type
    if (from->isARRAY() && to->isARRAY())
        return true;
    return from->isConst() ? (!to->isPTR() && !to->isFunc() && !to->isVoid()) : (from->isInt() || from->isFloat()) ? (!to->isPTR() && !to->isFunc() && !to->isVoid() && !to->isConst())
                                                                                                                   : false;
}

void clearTypes()
{
    for (auto type : newTypes)
        delete type;
}
