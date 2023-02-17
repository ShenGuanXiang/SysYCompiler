#include "Operand.h"
#include <sstream>
#include <algorithm>
#include <string.h>

std::string Operand::toStr() const
{
    auto type = se->getType();
    if (se->isVariable())
    {
        auto se_id = (IdentifierSymbolEntry *)se;
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

void Operand::removeUse(Instruction *inst)
{
    auto i = std::find(uses.begin(), uses.end(), inst);
    if (i != uses.end())
        uses.erase(i);
}

bool Operand::operator==(const Operand &a) const
{
    return this->se == a.se;
}

bool Operand::operator<(const Operand &a) const
{
    if (this->se->getKind() == a.se->getKind())
    {
        if (this->se->isConstant())
            return se->getValue() < a.se->getValue();
        if (this->se->isTemporary())
            return dynamic_cast<TemporarySymbolEntry *>(se)->getLabel() < dynamic_cast<TemporarySymbolEntry *>(a.se)->getLabel();
        if (this->se->isVariable())
            return dynamic_cast<IdentifierSymbolEntry *>(se)->getName() < dynamic_cast<IdentifierSymbolEntry *>(a.se)->getName();
    }
    return this->se->getKind() < a.se->getKind();
}
