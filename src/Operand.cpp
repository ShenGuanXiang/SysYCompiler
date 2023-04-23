#include "Operand.h"
#include <sstream>
#include <string.h>

extern FILE *yyout;

extern std::string Double2HexStr(double val);
extern std::string DeclArray(ArrayType *type, std::vector<double> initializer);

std::string Operand::toStr() const
{
    if (se->isVariable())
    {
        auto se_id = (IdentifierSymbolEntry *)se;
        auto type = se->getType();
        if (type->isConst() && !type->isARRAY())
        {
            return se_id->toStr();
        }
        else if (se_id->isGlobal())
        {
            return type->isFunc() ? se_id->toStr() : "@" + se_id->toStr();
        }
        assert(se_id->isParam());
        return se_id->toStr();
    }
    else
        return se->toStr();
}

// bool Operand::operator==(const Operand &a) const
// {
//     if (this->se->getKind() == a.se->getKind())
//     {
//         if (this->se->isConstant())
//             return se->getValue() == a.se->getValue();
//         if (this->se->isTemporary())
//             return dynamic_cast<TemporarySymbolEntry *>(se)->getLabel() == dynamic_cast<TemporarySymbolEntry *>(a.se)->getLabel();
//         if (this->se->isVariable())
//             return dynamic_cast<IdentifierSymbolEntry *>(se)->getName() == dynamic_cast<IdentifierSymbolEntry *>(a.se)->getName();
//     }
//     return false;
// }

// bool Operand::operator<(const Operand &a) const
// {
//     if (this->se->getKind() == a.se->getKind())
//     {
//         if (this->se->isConstant())
//             return se->getValue() < a.se->getValue();
//         if (this->se->isTemporary())
//             return dynamic_cast<TemporarySymbolEntry *>(se)->getLabel() < dynamic_cast<TemporarySymbolEntry *>(a.se)->getLabel();
//         if (this->se->isVariable())
//             return dynamic_cast<IdentifierSymbolEntry *>(se)->getName() < dynamic_cast<IdentifierSymbolEntry *>(a.se)->getName();
//     }
//     return this->se->getKind() < a.se->getKind();
// }

void IdentifierSymbolEntry::decl_code()
{
    if (type->isFunc())
    {
        if (name == "memset")
        {
            fprintf(yyout, "declare void @llvm.memset.p0.i32(i8*, i8, i32, i1)\n");
            fprintf(stderr, "declare void @llvm.memset.p0.i32(i8*, i8, i32, i1)\n");
        }
        else
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
    }
    else if (type->isARRAY())
    {
        if (getAddr() != nullptr && getAddr()->usersNum() != 0)
        {
            fprintf(yyout, "@%s = dso_local global ", this->toStr().c_str());
            fprintf(stderr, "@%s = dso_local global ", this->toStr().c_str());
            if (this->getNonZeroCnt() == 0)
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
            fprintf(yyout, "@%s = dso_local global %s %s, align 4\n", name.c_str(), type->toStr().c_str(), Double2HexStr((double(float(value)))).c_str());
            fprintf(stderr, "@%s = dso_local global %s %s, align 4\n", name.c_str(), type->toStr().c_str(), Double2HexStr((double(float(value)))).c_str());
        }
    }
}