#include "SymbolTable.h"
#include <Type.h>
#include <iostream>
#include <sstream>
#include <stack>

extern FILE *yyout;
static std::vector<SymbolEntry *> newSymbolEntries = std::vector<SymbolEntry *>(); // 用来回收new出来的SymbolEntry

// llvm的16进制float格式 reference: https://groups.google.com/g/llvm-dev/c/IlqV3TbSk6M/m/27dAggZOMb0J
std::string Double2HexStr(double val)
{
    union HEX
    {
        double num;
        unsigned char hex_num[8];
    } ptr;
    ptr.num = val;

    char hex_str[16] = {0};
    for (int i = 0; i < (int)sizeof(double); i++)
        snprintf(hex_str + 2 * i, 8, "%02X", ptr.hex_num[8 - i - 1]);
    if (('0' <= hex_str[8] && hex_str[8] <= '9' && hex_str[8] % 2 == 1) ||
        ('A' <= hex_str[8] && hex_str[8] <= 'F' && hex_str[8] % 2 == 0))
        hex_str[8] -= 1;
    return "0x" + std::string(hex_str, hex_str + 9) + std::string(7, '0');
}

// @a = dso_local global DeclArray, align 4
// eg : [2 x [3 x i32]] [[3 x i32] [i32 1, i32 2, i32 3], [3 x i32] [i32 0, i32 0, i32 0]]
std::string DeclArray(ArrayType *type, std::vector<double> initializer)
{
    std::string decl;
    auto elemType = type->getElemType();
    std::string type_str = type->getElemType()->toStr();
    auto dims = type->fetch();
    if (dims.size() == 1)
    {
        decl = type->toStr() + " [" + type_str + " " + (elemType->isFloat() ? Double2HexStr(double(float(initializer[0]))) : std::to_string((int)initializer[0]));
        for (size_t i = 1; i != initializer.size(); i++)
        {
            decl += ", " + type_str + " " + (elemType->isFloat() ? Double2HexStr(double(float(initializer[0]))) : std::to_string((int)initializer[i]));
        }
        decl += "]";
        return decl;
    }
    auto d = dims[0];
    auto next_type = new ArrayType(*type);
    dims.erase(dims.begin());
    next_type->setDim(dims);
    decl = type->toStr() + " [";
    for (int i = 0; i < d; i++)
    {
        if (i)
            decl += ", ";
        std::vector<double> next_initializer;
        for (int j = 0; j < next_type->getSize() / 4; j++)
        {
            next_initializer.push_back(initializer[0]);
            initializer.erase(initializer.begin());
        }
        decl += DeclArray(next_type, next_initializer);
    }
    decl += "]";
    return decl;
}

std::vector<std::string> lib_funcs{
    "getint",
    "getch",
    "getfloat",
    "getarray",
    "getfarray",
    "putint",
    "putch",
    "putfloat",
    "putarray",
    "putfarray",
    "memset",
    "_sysy_starttime",
    "_sysy_stoptime",
};

bool IdentifierSymbolEntry::isLibFunc()
{
    for (auto lib_func : lib_funcs)
        if (name == lib_func)
            return true;
    return false;
}

SymbolEntry::SymbolEntry(Type *type, int kind)
{
    this->type = type;
    this->kind = kind;
    arrVals = std::vector<double>();
    newSymbolEntries.push_back(this);
}

ConstantSymbolEntry::ConstantSymbolEntry(Type *type, double value) : SymbolEntry(Var2Const(type), SymbolEntry::CONSTANT)
{
    this->value = value;
}

std::string ConstantSymbolEntry::toStr()
{
    // assert(type->isConst());
    if (type->isConstInt() || type->isConstIntArray()) // const int / const bool
        return std::to_string((int)value);
    else
    {
        assert(type->isConstFloat());
        // return std::to_string((float)value);
        return Double2HexStr(double(float(value)));
    }
}

IdentifierSymbolEntry::IdentifierSymbolEntry(Type *type, std::string name, int scope) : SymbolEntry(type, SymbolEntry::VARIABLE), name(name)
{
    this->scope = scope;
    addr = nullptr;
    this->is8BytesAligned = type->isFunc() && isLibFunc() && name != "getint" && name != "putint" && name != "getch" && name != "putch" && name != "getarray" && name != "putarray";
    if (type->isFunc() && isLibFunc())
    {
        if (name == "getint" || name == "putint" || name == "getch" || name == "putch" || name == "getarray" || name == "putarray" || name == "putfarray" || name == "memset" || name == "_sysy_starttime" || name == "_sysy_stoptime")
        {
            occupiedRegs.insert(std::make_pair(0, TypeSystem::intType));
            occupiedRegs.insert(std::make_pair(1, TypeSystem::intType));
            occupiedRegs.insert(std::make_pair(2, TypeSystem::intType));
            occupiedRegs.insert(std::make_pair(3, TypeSystem::intType));
        }
        else if (name == "getfloat" || name == "putfloat" || name == "getfarray")
        {
            occupiedRegs.insert(std::make_pair(0, TypeSystem::floatType));
            occupiedRegs.insert(std::make_pair(0, TypeSystem::intType));
            occupiedRegs.insert(std::make_pair(1, TypeSystem::intType));
            occupiedRegs.insert(std::make_pair(2, TypeSystem::intType));
            occupiedRegs.insert(std::make_pair(3, TypeSystem::intType));
        }
        else
            assert(0);
    }
    func_se = nullptr;
    label = -1;
    paramOpe = nullptr;
}

void IdentifierSymbolEntry::addOccupiedReg(int reg_no, Type *type)
{
    assert(0 <= reg_no && reg_no <= 3 && (type == TypeSystem::floatType || type == TypeSystem::intType));
    int cnt = occupiedRegs.count(reg_no);
    if (cnt)
    {
        auto it = occupiedRegs.find(reg_no);
        for (int i = 0; i < cnt; i++, it++)
            if (it->second == type)
                return;
    }
    occupiedRegs.insert(std::make_pair(reg_no, type));
}

bool IdentifierSymbolEntry::paramMem2RegAble()
{
    assert(isParam());
    if (paramNo < 4)
    {
        int count = getFuncSe()->getOccupiedRegs().count(paramNo);
        if (!count)
            return true;
        auto it = getFuncSe()->getOccupiedRegs().find(paramNo);
        for (int i = 0; i < count; i++, it++)
            if ((it->second->isFloat() && type->isFloat()) || (it->second->isInt() && (type->isInt() /*|| type->isARRAY()*/ || type->isPTR())))
                return false;
        return true;
    }
    return false;
}

std::string IdentifierSymbolEntry::toStr()
{
    // 常量折叠
    if (type->isConst() && !type->isARRAY())
    {
        if (type->isConstInt())
            return std::to_string((int)value);
        else
        {
            assert(type->isConstFloat());
            // return std::to_string((float)value);
            return Double2HexStr(double(float(value)));
        }
    }
    else if (isGlobal())
    {
        return type->isFunc() ? "@" + name : name;
    }
    assert(isParam());
    return "%" + name;
}

std::string TemporarySymbolEntry::toStr()
{
    std::ostringstream buffer;
    buffer << "%t" << label;
    return buffer.str();
}

SymbolTable::SymbolTable()
{
    prev = nullptr;
    level = 0;
}

SymbolTable::SymbolTable(SymbolTable *prev)
{
    this->prev = prev;
    this->level = prev->level + 1;
}

bool match(std::vector<Type *> paramsType, std::vector<Type *> paramsType_found)
{
    if (paramsType.size() != paramsType_found.size())
        return false;
    for (size_t j = 0; j != paramsType_found.size(); j++)
        if (!convertible(paramsType[j], paramsType_found[j]))
            return false;
    return true;
}

/*
    Description: lookup the symbol entry of an identifier in the symbol table
    name: identifier name
    Return: pointer to the symbol entry of the identifier

    hint:
    1. The symbol table is a stack. The top of the stack contains symbol entries in the current scope.
    2. Search the entry in the current symbol table at first.
    3. If it's not in the current table, search it in previous ones(along the 'prev' link).
    4. If you find the entry, return it.
    5. If you can't find it in all symbol tables, return nullptr.
*/
SymbolEntry *SymbolTable::lookup(std::string name, bool isFunc, std::vector<Type *> paramsType)
{
    SymbolTable *t = this;
    while (t != nullptr)
    {
        if (int count = t->symbolTable.count(name))
        {
            std::multimap<std::string, SymbolEntry *>::iterator it = t->symbolTable.find(name);
            if (!isFunc)
            {
                assert((count == 1) && (!it->second->getType()->isFunc())); // 不支持同一作用域下变量和函数重名
                return it->second;
            }
            else
            {
                for (int i = 0; i < count; i++, it++)
                {
                    assert(it->second->getType()->isFunc());
                    std::vector<Type *> paramsType_found = ((FunctionType *)(it->second->getType()))->getParamsType();
                    if (match(paramsType, paramsType_found))
                        return it->second;
                }
            }
        }
        t = t->prev;
    }
    return nullptr;
}

// install the entry into current symbol table.
bool SymbolTable::install(std::string name, SymbolEntry *entry)
{
    // 检查参数数量和类型相同的函数重定义+变量同一作用域下重复声明
    int count = this->symbolTable.count(name);
    if (!count)
    {
        symbolTable.insert(std::make_pair(name, entry));
        return true;
    }
    else
    {
        if (entry->getType()->isFunc())
        {
            std::multimap<std::string, SymbolEntry *>::iterator it = this->symbolTable.find(name);
            for (int i = 0; i < count; i++, it++)
                if (!(it->second->getType()->isFunc()) || match((((FunctionType *)(entry->getType()))->getParamsType()), ((FunctionType *)(it->second->getType()))->getParamsType()))
                    return false;
            symbolTable.insert(std::make_pair(name, entry));
            return true;
        }
        else
            return false;
    }
}

int SymbolTable::counter = 0;
static SymbolTable t;
SymbolTable *identifiers = &t;
SymbolTable *globals = &t;

void clearSymbolEntries()
{
    for (auto &se : newSymbolEntries)
    {
        delete se;
        se = nullptr;
    }
}
