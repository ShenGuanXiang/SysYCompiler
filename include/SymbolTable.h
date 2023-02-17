#ifndef __SYMBOLTABLE_H__
#define __SYMBOLTABLE_H__

#include <string>
#include <map>
#include <vector>
#include <assert.h>
#include "Type.h"

class Type;
class Operand;
class MachineOperand;

class SymbolEntry
{
private:
    int kind;

protected:
    enum
    {
        CONSTANT,
        VARIABLE,
        TEMPORARY
    };
    Type *type;
    double value; // TemporarySymbolEntry用不到
    std::vector<double> arrVals;

public:
    SymbolEntry(Type *type, int kind);
    virtual ~SymbolEntry(){};
    bool isConstant() const { return kind == CONSTANT; };
    bool isTemporary() const { return kind == TEMPORARY; };
    bool isVariable() const { return kind == VARIABLE; };
    Type *getType() { return type; };
    void setType(Type *type) { this->type = type; };
    double getValue() { return value; };
    void setValue(double val) { value = val; };
    std::vector<double> getArrVals() { return arrVals; };
    void setArrVals(std::vector<double> arrVals) { this->arrVals = arrVals; };
    int getKind() { return kind; };
    virtual std::string toStr() = 0;
    // You can add any function you need here.
};


// symbol table managing identifier symbol entries
class SymbolTable
{
private:
    std::multimap<std::string, SymbolEntry *> symbolTable;
    SymbolTable *prev;
    int level;
    static int counter;

public:
    SymbolTable();
    SymbolTable(SymbolTable *prev);
    bool install(std::string name, SymbolEntry *entry);
    SymbolEntry *lookup(std::string name, bool isFunc = false, std::vector<Type *> ParamsType = std::vector<Type *>{});
    SymbolTable *getPrev() { return prev; };
    int getLevel() { return level; };
    static int getLabel() { return counter++; };
};

/*
    Symbol entry for literal constant. Example:

    int a = 1;

    Compiler should create constant symbol entry for literal constant '1'.
*/
class ConstantSymbolEntry : public SymbolEntry
{
public:
    ConstantSymbolEntry(Type *type, double value);
    virtual ~ConstantSymbolEntry(){};
    std::string toStr();
    // You can add any function you need here.
};

/*
    Symbol entry for identifier. Example:

    int a;
    int b;
    void f(int c)
    {
        int d;
        {
            int e;
        }
    }

    Compiler should create identifier symbol entries for variables a, b, c, d and e:

    | variable | scope    |
    | a        | GLOBAL   |
    | b        | GLOBAL   |
    | c        | PARAM    |
    | d        | LOCAL    |
    | e        | LOCAL +1 |
*/
class IdentifierSymbolEntry : public SymbolEntry
{
private:
    enum
    {
        GLOBAL,
        PARAM,
        LOCAL
    };
    std::string name;
    int label;
    int scope;
    Operand *addr;        // The address of the identifier.
    int paramNo;          // if is param
    bool is8BytesAligned; // func sp needs to be 8 bytes aligned for public interface call  https://www.cse.scu.edu/~dlewis/book3/docs/StackAlignment.pdf
    // You can add any field you need here.

public:
    IdentifierSymbolEntry(Type *type, std::string name, int scope);
    virtual ~IdentifierSymbolEntry(){};
    std::string toStr();
    bool isGlobal() const { return scope == GLOBAL; };
    bool isParam() const { return scope == PARAM; };
    bool isLocal() const { return scope >= LOCAL; };
    int getScope() const { return scope; };
    void setAddr(Operand *addr) { this->addr = addr; };
    Operand *getAddr() { return addr; };
    // You can add any function you need here.
    void setParamNo(int paramNo) { this->paramNo = paramNo; };
    int getParamNo() { return paramNo; };
    std::string getName() const { return name; };
    void setLabel() { label = SymbolTable::getLabel(); };
    bool isLibFunc();
    void decl_code();
    bool need8BytesAligned() { return this->is8BytesAligned; };
    void set8BytesAligned() { this->is8BytesAligned = true; };
};

/*
    Symbol entry for temporary variable created by compiler. Example:

    int a;
    a = 1 + 2 + 3;

    The compiler would generate intermediate code like:

    t1 = 1 + 2
    t2 = t1 + 3
    a = t2

    So compiler should create temporary symbol entries for t1 and t2:

    | temporary variable | label |
    | t1                 | 1     |
    | t2                 | 2     |
*/
class TemporarySymbolEntry : public SymbolEntry
{
private:
    int stack_offset;
    int label;
    bool isArray;

public:
    TemporarySymbolEntry(Type *type, int label, bool isarray = false) : SymbolEntry(type, SymbolEntry::TEMPORARY)
    {
        this->label = label;
        this->isArray = isarray;
        this->stack_offset = 0;
    };
    virtual ~TemporarySymbolEntry(){};
    std::string toStr();
    int getLabel() const { return label; };
    void setArray() { isArray = true; };
    void setOffset(int offset) { this->stack_offset = offset; };
    int getOffset() { return this->stack_offset; };
    // You can add any function you need here.
};


extern SymbolTable *identifiers;
extern SymbolTable *globals;

bool match(std::vector<Type *> paramsType, std::vector<Type *> paramsType_found);

#endif
