#include "Operand.h"
#include <sstream>
#include <string.h>

extern FILE *yyout;

extern std::string Double2HexStr(double val);
extern std::string DeclArray(ArrayType *type, std::vector<double> initializer);

static std::vector<Operand *> newOperands;

Operand::Operand(SymbolEntry *se) : se(se)
{
    defs = std::set<Instruction *>();
    uses = std::set<Instruction *>();
};

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
        else if (name == "_mulmod")
        {
            fprintf(yyout, "define dso_local i32 @_mulmod(i32 %%b, i32 %%a, i32 %%mod) #0{\n");
            fprintf(yyout, "B_13: %*c; succs = %%B_14, %%B_18\n", 32, '\t');
            fprintf(yyout, "  %%t_0 = icmp slt i32 0, %%b\n");
            fprintf(yyout, "  br i1 %%t_0, label %%B_18, label %%B_14\n");
            fprintf(yyout, "B_14: %*c; preds = %%B_13, %%B_18\n", 32, '\t');
            fprintf(yyout, "  %%t_37 = phi i32 [ 0 , %%B_13 ], [ %%t_2 , %%B_18 ]\n");
            fprintf(yyout, "  ret i32 %%t_37\n");
            fprintf(yyout, "B_18: %*c; preds = %%B_13, %%B_18%*c; succs = %%B_14, %%B_18\n", 32, '\t', 32, '\t');
            fprintf(yyout, "  %%t_35 = phi i32 [ 0 , %%B_13 ], [ %%t_3 , %%B_18 ]\n");
            fprintf(yyout, "  %%t_36 = phi i32 [ 0 , %%B_13 ], [ %%t_2 , %%B_18 ]\n");
            fprintf(yyout, "  %%t_1 = add i32 %%t_36, %%a\n");
            fprintf(yyout, "  %%t_2 = srem i32 %%t_1, %%mod\n");
            fprintf(yyout, "  %%t_3 = add i32 %%t_35, 1\n");
            fprintf(yyout, "  %%t_23 = icmp slt i32 %%t_3, %%b\n");
            fprintf(yyout, "  br i1 %%t_23, label %%B_18, label %%B_14\n");
            fprintf(yyout, "}\n");
        }
        else if (name == "__create_threads")
        {
            // TODO: LLVM for threadFuncs 

        }
        else if (name == "__join_threads")
        {
            // TODO: LLVM for threadFuncs 
            
        }
        else if (name == "__bind_core")
        {
            // TODO: LLVM for threadFuncs 
            
        }
        else if (name == "__lock")
        {
            // TODO: LLVM for threadFuncs 
            
        }
        else if (name == "__unlock")
        {
            // TODO: LLVM for threadFuncs 
            
        }
        else if (name == "__barrier")
        {
            // TODO: LLVM for threadFuncs 
            
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

void clearOperands()
{
    for (auto &op : newOperands)
    {
        delete op;
        op = nullptr;
    }
}