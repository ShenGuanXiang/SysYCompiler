#include "SymbolTable.h"
#include <Type.h>
#include <iostream>
#include <sstream>
#include <stack>

extern FILE *yyout;
static std::vector<SymbolEntry *> newSymbolEntries; // 用来回收new出来的SymbolEntry

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
        decl = type->toStr() + " [" + type_str + " " + (elemType->isFloat() ? Double2HexStr(initializer[0]) : std::to_string((int)initializer[0]));
        for (size_t i = 1; i != initializer.size(); i++)
        {
            decl += ", " + type_str + " " + (elemType->isFloat() ? Double2HexStr(initializer[i]) : std::to_string((int)initializer[i]));
        }
        decl += "]";
        return decl;
    }
    auto d = dims[0];
    auto next_type = new ArrayType(*type);
    dims.erase(dims.begin());
    next_type->SetDim(dims);
    decl = type->toStr() + " [";
    for (int i = 0; i < d; i++)
    {
        if (i)
            decl += ", ";
        std::vector<double> next_initializer;
        for (int j = 0; j < (int)next_type->getSize() / 4; j++)
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
    "putfarray"};

bool IdentifierSymbolEntry::isLibFunc()
{
    for (auto lib_func : lib_funcs)
        if (name == lib_func)
            return true;
    return false;
}

void IdentifierSymbolEntry::decl_code()
{
    if (type->isFunc())
    {
        fprintf(yyout, "declare %s @%s(",
                dynamic_cast<FunctionType *>(type)->getRetType()->toStr().c_str(), name.c_str());
        fprintf(stderr, "declare %s @%s(",
                dynamic_cast<FunctionType *>(type)->getRetType()->toStr().c_str(), name.c_str());
        std::vector<Type *> paramslist = dynamic_cast<FunctionType *>(type)->getParamsType();
        for (auto it = paramslist.begin(); it != paramslist.end(); it++)
        {
            if (it != paramslist.begin())
            {
                fprintf(yyout, ", ");
                fprintf(stderr, ", ");
            }
            fprintf(yyout, "%s", (*it)->toStr().c_str());
            fprintf(stderr, "%s", (*it)->toStr().c_str());
        }
        fprintf(yyout, ")\n");
        fprintf(stderr, ")\n");
    }
    else if (type->isARRAY())
    {
        fprintf(yyout, "@%s = dso_local global ", this->toStr().c_str());
        fprintf(stderr, "@%s = dso_local global ", this->toStr().c_str());
        if (((IdentifierSymbolEntry *)this)->getArrVals().empty())
        {
            fprintf(yyout, "%s zeroinitializer", type->toStr().c_str());
            fprintf(stderr, "%s zeroinitializer", type->toStr().c_str());
        }
        else
        {
            fprintf(yyout, "%s", DeclArray((ArrayType *)type, getArrVals()).c_str());
            fprintf(stderr, "%s", DeclArray((ArrayType *)type, getArrVals()).c_str());
        }
        fprintf(yyout, ", align 4\n");
        fprintf(stderr, ", align 4\n");
    }
    else
    {
        if (type->isConst()) // 常量折叠
            ;
        else if (type->isInt())
        {
            fprintf(yyout, "@%s = dso_local global %s %d, align 4\n", name.c_str(), type->toStr().c_str(), (int)value);
            fprintf(stderr, "@%s = dso_local global %s %d, align 4\n", name.c_str(), type->toStr().c_str(), (int)value);
        }
        else if (type->isFloat())
        {
            fprintf(yyout, "@%s = dso_local global %s %s, align 4\n", name.c_str(), type->toStr().c_str(), Double2HexStr(value).c_str());
            fprintf(stderr, "@%s = dso_local global %s %s, align 4\n", name.c_str(), type->toStr().c_str(), Double2HexStr(value).c_str());
        }
    }
}

SymbolEntry::SymbolEntry(Type *type, int kind)
{
    this->type = type;
    this->kind = kind;
    arrVals = std::vector<double>();
    newSymbolEntries.push_back(this);
}

ConstantSymbolEntry::ConstantSymbolEntry(Type *type, double value) : SymbolEntry(type, SymbolEntry::CONSTANT)
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
        return Double2HexStr(value);
    }
}

IdentifierSymbolEntry::IdentifierSymbolEntry(Type *type, std::string name, int scope) : SymbolEntry(type, SymbolEntry::VARIABLE), name(name)
{
    this->scope = scope;
    addr = nullptr;
    this->is8BytesAligned = type->isFunc() && isLibFunc() && name != "getint" && name != "putint" && name != "getch" && name != "putch"; // ToDo ：getarray和putarray需要8字节对齐嘛？
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
            return Double2HexStr(value);
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
    Parameters:
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
    for (auto se : newSymbolEntries)
        delete se;
}
