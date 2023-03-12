#include "MachineCode.h"
extern FILE *yyout;

static std::vector<MachineOperand *> newMachineOperands; // 用来回收new出来的SymbolEntry

// 合法的第二操作数循环移位偶数位后可以用8bit表示
bool isShifterOperandVal(unsigned bin_val)
{
    for (int i = 0; i < 32; i += 2)
    {
        unsigned shift_val = ((bin_val) >> i) | ((bin_val) << (32 - i)); // 循环右移i位
        if ((shift_val & 0xFFFFFF00) == 0x00000000)
            return true;
    }
    return false;
}

// TODO
bool isSignedShifterOperandVal(signed signed_val)
{
    unsigned bin_val = reinterpret_cast<unsigned &>(signed_val);
    return signed_val >= 0 ? isShifterOperandVal(bin_val) : signed_val >= -255;
}

// TODO
bool isFloatShifterOperandVal(float float_val)
{
    return false;
}

MachineOperand::MachineOperand(int tp, double val, Type *valType)
{
    this->type = tp;
    if (tp == MachineOperand::IMM)
        this->val = val;
    else
        this->reg_no = (int)val;
    // 约定MachineOperand的valType是int/float/bool
    assert(!valType->isARRAY());
    assert(!valType->isPTR());
    this->valType = valType;
    this->parent = nullptr;
    newMachineOperands.push_back(this);
}

MachineOperand::MachineOperand(std::string label)
{
    this->type = MachineOperand::LABEL;
    this->label = label;
    this->valType = TypeSystem::intType;
    newMachineOperands.push_back(this);
}

bool MachineOperand::operator==(const MachineOperand &a) const
{
    if (this->type != a.type)
        return false;
    if (this->type == IMM)
        return this->val == a.val;
    if (this->type == LABEL)
        return this->label == a.label;
    return this->reg_no == a.reg_no && ((this->valType->isFloat() && a.valType->isFloat()) || (this->valType->isInt() && a.valType->isInt()));
}

bool MachineOperand::operator<(const MachineOperand &a) const
{
    if (this->type == a.type)
    {
        if (this->type == IMM)
            return this->val < a.val;
        if (this->type == LABEL)
            return this->label < a.label; // 不太理解比较label的意义
        assert(this->type == VREG || this->type == REG);
        if (this->valType->isInt() && a.valType->isFloat())
            return true;
        if (this->valType->isFloat() && a.valType->isInt())
            return false;
        return this->reg_no < a.reg_no;
    }
    return this->type < a.type;
}

void MachineOperand::printReg()
{
    if (valType->isFloat())
    {
        switch (reg_no)
        {
        // case 32:
        //     fprintf(yyout, "FPSCR");
        //     break;
        default:
            fprintf(yyout, "s%d", reg_no);
            break;
        }
    }
    else
    {
        assert(valType->isInt());
        switch (reg_no)
        {
        case 11:
            fprintf(yyout, "fp");
            break;
        case 13:
            fprintf(yyout, "sp");
            break;
        case 14:
            fprintf(yyout, "lr");
            break;
        case 15:
            fprintf(yyout, "pc");
            break;
        default:
            fprintf(yyout, "r%d", reg_no);
            break;
        }
    }
}

void MachineOperand::output()
{
    /* HINT：print operand
     * Example:
     * immediate num 1 -> print #1;
     * register 1 -> print r1;
     * label addr_a -> print addr_a; */
    switch (this->type)
    {
    case IMM:
    {
        if (valType->isFloat())
        {
            float float_val = (float)(this->val);
            fprintf(yyout, "#%u", reinterpret_cast<unsigned &>(float_val));
        }
        else
        {
            assert(valType->isInt());
            fprintf(yyout, "#%d", (int)this->val);
        }
        break;
    }
    case VREG:
    {
        assert(valType->isInt() || valType->isFloat());
        std::string str = valType->isFloat() ? "vs" : "vr";
        fprintf(yyout, "%s%d", str.c_str(), this->reg_no);
        break;
    }
    case REG:
    {
        printReg();
        break;
    }
    case LABEL:
    {
        if (this->label.substr(0, 2) == ".L")
            fprintf(yyout, "%s", this->label.c_str());
        else if (this->label.substr(0, 1) == "@")
            fprintf(yyout, "%s", this->label.c_str() + 1);
        else
            fprintf(yyout, "addr_%d_%s", parent->getParent()->getParent()->getParent()->getLtorgNo(), /*this->label.substr(0, 1) == "@" ? this->label.c_str() + 1 : */ this->label.c_str());
        break;
    }
    default:
        break;
    }
}

bool MachineOperand::isIllegalShifterOperand()
{
    assert(this->isImm());
    if (valType->isFloat())
        return !isFloatShifterOperandVal((float)(this->getVal()));
    assert(valType->isInt());
    return !isSignedShifterOperandVal((int)(this->val));
}

void MachineInstruction::printCond()
{
    switch (cond)
    {
    case EQ:
        fprintf(yyout, "eq");
        break;
    case NE:
        fprintf(yyout, "ne");
        break;
    case LT:
        fprintf(yyout, "lt");
        break;
    case LE:
        fprintf(yyout, "le");
        break;
    case GT:
        fprintf(yyout, "gt");
        break;
    case GE:
        fprintf(yyout, "ge");
        break;
    default:
        break;
    }
}

MachineInstruction::~MachineInstruction()
{
    if (parent != nullptr)
        parent->removeInst(this);
}

BinaryMInstruction::BinaryMInstruction(
    MachineBlock *p, int op,
    MachineOperand *dst, MachineOperand *src1, MachineOperand *src2,
    int cond)
{
    this->parent = p;
    this->type = MachineInstruction::BINARY;
    this->op = op;
    this->cond = cond;
    this->def_list.push_back(dst);
    this->use_list.push_back(src1);
    this->use_list.push_back(src2);
    dst->setParent(this);
    src1->setParent(this);
    src2->setParent(this);
}

void BinaryMInstruction::output()
{
    if (def_list[0]->getValType()->isFloat())
    {
        switch (this->op)
        {
        case BinaryMInstruction::ADD:
            fprintf(yyout, "\tvadd.f32");
            break;
        case BinaryMInstruction::SUB:
            fprintf(yyout, "\tvsub.f32");
            break;
        case BinaryMInstruction::MUL:
            fprintf(yyout, "\tvmul.f32");
            break;
        case BinaryMInstruction::DIV:
            fprintf(yyout, "\tvdiv.f32");
            break;
        default:
            break;
        }
    }
    else
    {
        switch (this->op)
        {
        case BinaryMInstruction::ADD:
            fprintf(yyout, "\tadd");
            break;
        case BinaryMInstruction::SUB:
            fprintf(yyout, "\tsub");
            break;
        case BinaryMInstruction::MUL:
            fprintf(yyout, "\tmul");
            break;
        case BinaryMInstruction::DIV:
            fprintf(yyout, "\tsdiv");
            break;
        default:
            break;
        }
    }
    printCond();
    fprintf(yyout, " ");
    this->def_list[0]->output();
    fprintf(yyout, ", ");
    this->use_list[0]->output();
    fprintf(yyout, ", ");
    this->use_list[1]->output();
    fprintf(yyout, "\n");
}

LoadMInstruction::LoadMInstruction(MachineBlock *p,
                                   MachineOperand *dst, MachineOperand *src1, MachineOperand *src2,
                                   int cond)
{
    this->parent = p;
    this->type = MachineInstruction::LOAD;
    this->op = -1;
    this->cond = cond;
    this->def_list.push_back(dst);
    this->use_list.push_back(src1);
    if (src2)
        this->use_list.push_back(src2);
    dst->setParent(this);
    src1->setParent(this);
    if (src2)
        src2->setParent(this);
}

void LoadMInstruction::output()
{
    // 强度削弱：小的立即数用MOV/MVN优化一下，arm汇编器会自动做?
    if ((this->use_list.size() == 1) && this->use_list[0]->isImm() && !this->use_list[0]->isIllegalShifterOperand())
    {
        if (this->def_list[0]->getValType()->isFloat())
            fprintf(yyout, "\tvmov.f32");
        else
            fprintf(yyout, "\tmov");
        printCond();
        fprintf(yyout, " ");
        this->def_list[0]->output();
        fprintf(yyout, ", ");
        this->use_list[0]->output();
        fprintf(yyout, "\n");
        return;
    }
    if ((this->use_list.size() == 1) && this->use_list[0]->isImm() && this->use_list[0]->getValType()->isInt() && isSignedShifterOperandVal(~((int)this->use_list[0]->getVal())))
    {
        fprintf(yyout, "\tmvn");
        printCond();
        fprintf(yyout, " ");
        this->def_list[0]->output();
        fprintf(yyout, ", ");
        auto negative_imm = new MachineOperand(MachineOperand::IMM, ~((int)this->use_list[0]->getVal()), this->use_list[0]->getValType());
        negative_imm->output();
        fprintf(yyout, "\n");
        return;
    }

    if (this->def_list[0]->getValType()->isFloat())
        fprintf(yyout, "\tvldr.32");
    else if (this->def_list[0]->getValType()->isBool())
        fprintf(yyout, "\tldrb");
    else
        fprintf(yyout, "\tldr");
    printCond();
    fprintf(yyout, " ");
    this->def_list[0]->output();
    fprintf(yyout, ", ");

    // Load immediate num, eg: ldr r1, =8
    if (this->use_list[0]->isImm())
    {
        if (this->use_list[0]->getValType()->isFloat())
        {
            float float_val = (float)(this->use_list[0]->getVal());
            fprintf(yyout, "=%u\n", reinterpret_cast<unsigned &>(float_val));
        }
        else
            fprintf(yyout, "=%d\n", (int)this->use_list[0]->getVal());
        return;
    }

    // Load address
    if (this->use_list[0]->isReg() || this->use_list[0]->isVReg())
        fprintf(yyout, "[");

    this->use_list[0]->output();
    if (this->use_list.size() > 1)
    {
        fprintf(yyout, ", ");
        this->use_list[1]->output();
    }

    if (this->use_list[0]->isReg() || this->use_list[0]->isVReg())
        fprintf(yyout, "]");
    fprintf(yyout, "\n");
}

StoreMInstruction::StoreMInstruction(MachineBlock *p,
                                     MachineOperand *src1, MachineOperand *src2, MachineOperand *src3,
                                     int cond)
{
    this->parent = p;
    this->type = MachineInstruction::STORE;
    this->op = -1;
    this->cond = cond;
    this->use_list.push_back(src1);
    this->use_list.push_back(src2);
    if (src3)
        this->use_list.push_back(src3);
    src1->setParent(this);
    src2->setParent(this);
    if (src3)
        src3->setParent(this);
}

void StoreMInstruction::output()
{
    if (this->use_list[0]->getValType()->isFloat())
        fprintf(yyout, "\tvstr.32");
    else if (this->use_list[0]->getValType()->isBool())
        fprintf(yyout, "\tstrb");
    else
        fprintf(yyout, "\tstr");
    printCond();
    fprintf(yyout, " ");
    this->use_list[0]->output();
    fprintf(yyout, ", ");

    // Store address
    if (this->use_list[1]->isReg() || this->use_list[1]->isVReg())
        fprintf(yyout, "[");

    this->use_list[1]->output();
    if (this->use_list.size() > 2)
    {
        fprintf(yyout, ", ");
        this->use_list[2]->output();
    }

    if (this->use_list[1]->isReg() || this->use_list[1]->isVReg())
        fprintf(yyout, "]");
    fprintf(yyout, "\n");
}

MovMInstruction::MovMInstruction(MachineBlock *p, int op,
                                 MachineOperand *dst, MachineOperand *src,
                                 MachineOperand *shifter,
                                 int cond)
{
    // assert(!src->isImm());
    this->parent = p;
    this->type = MachineInstruction::MOV;
    this->op = op;
    this->cond = cond;
    this->def_list.push_back(dst);
    this->use_list.push_back(src);
    dst->setParent(this);
    src->setParent(this);
    if (shifter != nullptr)
    {
        // assert(op == MOVASR || op == MOVLSL || op == MOVLSR);
        this->use_list.push_back(shifter);
        shifter->setParent(this);
    }
}

void MovMInstruction::output()
{
    if ((*def_list[0]) == (*use_list[0]))
        return;
    switch (this->op)
    {
    case MovMInstruction::MOV:
    case MovMInstruction::MOVLSL:
        // case MovMInstruction::MOVLSR:
        // case MovMInstruction::MOVASR:
        if (use_list[0]->getValType()->isBool())
            fprintf(yyout, "\tmovw"); // move byte指令呢?
        else
        {
            // assert(use_list[0]->getValType()->isInt());
            fprintf(yyout, "\tmov");
        }
        break;
    // case MovMInstruction::MVN:
    //     fprintf(yyout, "\tmvn");
    //     break;
    // case MovMInstruction::MOVT:
    //     fprintf(yyout, "\tmovt");
    //     break;
    case MovMInstruction::VMOV:
        fprintf(yyout, "\tvmov");
        break;
    // case MovMInstruction::VMOVF32:
    //     fprintf(yyout, "\tvmov.f32");
    //     break;

    default:
        break;
    }
    printCond();
    fprintf(yyout, " ");
    this->def_list[0]->output();
    fprintf(yyout, ", ");
    this->use_list[0]->output();
    switch (this->op)
    {
    case MovMInstruction::MOVLSL:
        fprintf(yyout, ", LSL#%d", this->mov_num);
        break;
    
    default:
        break;
    }

    fprintf(yyout, "\n");
}

BranchMInstruction::BranchMInstruction(MachineBlock *p, int op,
                                       MachineOperand *dst,
                                       int cond)
{
    this->type = MachineInstruction::BRANCH;
    this->op = op;
    this->parent = p;
    this->cond = cond;
    this->def_list.push_back(dst);
    dst->setParent(this);
}

void BranchMInstruction::output()
{
    switch (op)
    {
    case BL:
        fprintf(yyout, "\tbl");
        break;
    case B:
        fprintf(yyout, "\tb");
        break;
    // case BX:
    //     fprintf(yyout, "\tbx ");
    //     break;
    default:
        break;
    }
    printCond();
    fprintf(yyout, " ");
    this->def_list[0]->output();
    fprintf(yyout, "\n");
}

CmpMInstruction::CmpMInstruction(MachineBlock *p,
                                 MachineOperand *src1, MachineOperand *src2,
                                 int cond)
{
    this->type = MachineInstruction::CMP;
    this->parent = p;
    this->op = cond;
    this->use_list.push_back(src1);
    this->use_list.push_back(src2);
    src1->setParent(this);
    src2->setParent(this);
}

void CmpMInstruction::output()
{
    if (this->use_list[0]->getValType()->isFloat())
        fprintf(yyout, "\tvcmp.f32");
    else
        fprintf(yyout, "\tcmp");
    printCond();
    fprintf(yyout, " ");
    this->use_list[0]->output();
    fprintf(yyout, ", ");
    this->use_list[1]->output();
    fprintf(yyout, "\n");
}

StackMInstruction::StackMInstruction(MachineBlock *p, int op,
                                     MachineOperand *src,
                                     int cond)
{
    this->parent = p;
    this->type = MachineInstruction::STACK;
    this->op = op;
    this->cond = cond;
    this->use_list.push_back(src);
    src->setParent(this);
}

StackMInstruction::StackMInstruction(MachineBlock *p, int op,
                                     std::vector<MachineOperand *> src,
                                     int cond)
{
    this->parent = p;
    this->type = MachineInstruction::STACK;
    this->op = op;
    this->cond = cond;
    this->use_list = src;
    for (auto mope : use_list)
    {
        mope->setParent(this);
    }
}

void StackMInstruction::output()
{
    assert(this->cond == NONE);
    std::string op_str;
    if (this->use_list[0]->getValType()->isFloat())
    {
        switch (op)
        {
        case PUSH:
            op_str = "\tvpush {";
            break;
        case POP:
            op_str = "\tvpop {";
            break;
        }
    }
    else
    {
        switch (op)
        {
        case PUSH:
            op_str = "\tpush {";
            break;
        case POP:
            op_str = "\tpop {";
            break;
        }
    }
    // 每次只能push/pop16个
    size_t i = 0;
    while (i != use_list.size())
    {
        fprintf(yyout, "%s", op_str.c_str());
        this->use_list[i++]->output();
        for (size_t j = 1; i != use_list.size() && j < 16; i++, j++)
        {
            fprintf(yyout, ", ");
            this->use_list[i]->output();
        }
        fprintf(yyout, "}\n");
    }
}

ZextMInstruction::ZextMInstruction(MachineBlock *p, MachineOperand *dst, MachineOperand *src, int cond)
{
    this->parent = p;
    this->type = MachineInstruction::ZEXT;
    this->cond = cond;
    this->def_list.push_back(dst);
    this->use_list.push_back(src);
    dst->setParent(this);
    src->setParent(this);
}

void ZextMInstruction::output()
{
    fprintf(yyout, "\tuxtb");
    printCond();
    fprintf(yyout, " ");
    def_list[0]->output();
    fprintf(yyout, ", ");
    use_list[0]->output();
    fprintf(yyout, "\n");
}

VcvtMInstruction::VcvtMInstruction(MachineBlock *p,
                                   int op,
                                   MachineOperand *dst,
                                   MachineOperand *src,
                                   int cond)
{
    this->parent = p;
    this->type = MachineInstruction::VCVT;
    this->op = op;
    this->cond = cond;
    this->def_list.push_back(dst);
    this->use_list.push_back(src);
    dst->setParent(this);
    src->setParent(this);
}

void VcvtMInstruction::output()
{
    switch (this->op)
    {
    case VcvtMInstruction::F2S:
        fprintf(yyout, "\tvcvt.s32.f32");
        break;
    case VcvtMInstruction::S2F:
        fprintf(yyout, "\tvcvt.f32.s32");
        break;
    default:
        break;
    }
    printCond();
    fprintf(yyout, " ");
    this->def_list[0]->output();
    fprintf(yyout, ", ");
    this->use_list[0]->output();
    fprintf(yyout, "\n");
}

VmrsMInstruction::VmrsMInstruction(MachineBlock *p)
{
    this->parent = p;
    this->type = MachineInstruction::VMRS;
}

void VmrsMInstruction::output()
{
    fprintf(yyout, "\tvmrs APSR_nzcv, FPSCR\n");
}

void MachineBlock::insertBefore(MachineInstruction *pos, MachineInstruction *inst)
{
    auto p = find(inst_list.begin(), inst_list.end(), pos);
    inst_list.insert(p, inst);
}

void MachineBlock::insertAfter(MachineInstruction *pos, MachineInstruction *inst)
{
    auto p = find(inst_list.begin(), inst_list.end(), pos);
    if (p == inst_list.end())
    {
        inst_list.push_back(inst);
        return;
    }
    inst_list.insert(p + 1, inst);
}

MachineBlock::~MachineBlock()
{
    auto delete_list = inst_list;
    for (auto inst : delete_list)
        delete inst;
    parent->removeBlock(this);
}

MachineOperand *MachineBlock::insertLoadImm(MachineOperand *imm)
{
    // auto internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), imm->getValType());
    // this->insertInst(new LoadMInstruction(this, internal_reg, imm));
    // return new MachineOperand(*internal_reg);

    // ToDo:有些浮点字面常量可以直接vldr到s寄存器
    MachineOperand *internal_reg1 = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::intType);
    this->insertInst(new LoadMInstruction(this, internal_reg1, imm));
    if (imm->getValType()->isFloat())
    {
        MachineOperand *internal_reg2 = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);
        internal_reg1 = new MachineOperand(*internal_reg1);
        this->insertInst(new MovMInstruction(this, MovMInstruction::VMOV, internal_reg2, internal_reg1));
        return new MachineOperand(*internal_reg2);
    }
    assert(imm->getValType()->isInt());
    return new MachineOperand(*internal_reg1);
}

void MachineBlock::output()
{
    fprintf(yyout, ".L%d:", this->no);
    if (!pred.empty())
    {
        fprintf(yyout, "%*c@ preds = .L%d", 32, '\t', pred[0]->getNo());
        for (auto i = pred.begin() + 1; i != pred.end(); i++)
            fprintf(yyout, ", .L%d", (*i)->getNo());
    }
    if (!succ.empty())
    {
        fprintf(yyout, "%*c@ succs = .L%d", 32, '\t', succ[0]->getNo());
        for (auto i = succ.begin() + 1; i != succ.end(); ++i)
            fprintf(yyout, ", .L%d", (*i)->getNo());
    }
    fprintf(yyout, "\n");
    int cnt = 0;
    for (auto iter : inst_list)
    {
        iter->output();
        cnt++;
        if (cnt > 300)
        {
            fprintf(yyout, "\tb .LiteralPool%d\n", parent->getParent()->getLtorgNo());
            fprintf(yyout, ".LTORG\n");
            parent->getParent()->printBridge();
            fprintf(yyout, ".LiteralPool%d:\n", parent->getParent()->getLtorgNo() - 1);
            cnt = 0;
        }
    }
}

MachineFunction::MachineFunction(MachineUnit *p, SymbolEntry *sym_ptr)
{
    this->parent = p;
    this->sym_ptr = sym_ptr;
    this->stack_size = 0;
};

void MachineFunction::addSavedRegs(int regno, bool is_sreg)
{
    if (is_sreg)
        saved_sregs.insert(saved_sregs.lower_bound(regno), regno);
    else
        saved_rregs.insert(saved_rregs.lower_bound(regno), regno);
}

std::vector<MachineOperand *> MachineFunction::getSavedRRegs()
{
    std::vector<MachineOperand *> regs;
    for (auto no : saved_rregs)
    {
        MachineOperand *reg = nullptr;
        reg = new MachineOperand(MachineOperand::REG, no);
        regs.push_back(reg);
    }
    return regs;
}

std::vector<MachineOperand *> MachineFunction::getSavedSRegs()
{
    std::vector<MachineOperand *> sregs;
    for (auto no : saved_sregs)
    {
        MachineOperand *sreg = nullptr;
        sreg = new MachineOperand(MachineOperand::REG, no, TypeSystem::floatType);
        sregs.push_back(sreg);
    }
    return sregs;
}

void MachineFunction::outputStart()
{
    // Save callee saved int registers
    fprintf(yyout, "\tpush {");
    std::vector<MachineOperand *> regs = getSavedRRegs();
    regs[0]->output();
    size_t i;
    for (i = 1; i != regs.size(); i++)
    {
        fprintf(yyout, ", ");
        regs[i]->output();
    }
    fprintf(yyout, "}\n");
    // Save callee saved float registers
    std::vector<MachineOperand *> sregs = getSavedSRegs();
    i = 0;
    while (i != sregs.size())
    {
        fprintf(yyout, "\tvpush {");
        sregs[i++]->output();
        for (int j = 1; i != sregs.size() && j != 16; i++, j++)
        {
            fprintf(yyout, ", ");
            sregs[i]->output();
        }
        fprintf(yyout, "}\n");
    }
    // fp = sp
    fprintf(yyout, "\tmov fp, sp\n");
    if (dynamic_cast<IdentifierSymbolEntry *>(sym_ptr)->need8BytesAligned() &&
        (4 * (regs.size() + sregs.size() + std::max(0, (int)dynamic_cast<FunctionType *>(sym_ptr->getType())->getParamsType().size() - 4)) + stack_size) % 8)
        stack_size += 4;
    // Allocate stack space for local variable
    if (stack_size)
    {
        if (!isShifterOperandVal(stack_size))
        {
            auto reg_no = (*saved_rregs.begin());
            assert(reg_no < 11);
            auto inst = new LoadMInstruction(nullptr, new MachineOperand(MachineOperand::REG, reg_no), new MachineOperand(MachineOperand::IMM, stack_size));
            inst->output(); // fprintf(yyout, "\tldr r%d, =%d\n", reg_no, stack_size);
            delete inst;
            fprintf(yyout, "\tsub sp, sp, r%d\n", reg_no);
        }
        else
            fprintf(yyout, "\tsub sp, sp, #%d\n", stack_size);
    }
}

void MachineFunction::outputEnd()
{
    // recycle stack space
    if (stack_size)
        fprintf(yyout, "\tmov sp, fp\n");
    // Restore saved registers
    std::vector<MachineOperand *> sregs = getSavedSRegs();
    size_t i = 0;
    while (i != sregs.size())
    {
        fprintf(yyout, "\tvpop {");
        sregs[i++]->output();
        for (int j = 1; i != sregs.size() && j != 16; i++, j++)
        {
            fprintf(yyout, ", ");
            sregs[i]->output();
        }
        fprintf(yyout, "}\n");
    }
    std::vector<MachineOperand *> regs = getSavedRRegs();
    fprintf(yyout, "\tpop {");
    regs[0]->output();
    for (i = 1; i != regs.size(); i++)
    {
        fprintf(yyout, ", ");
        regs[i]->output();
    }
    fprintf(yyout, "}\n");
    // Generate bx instruction
    fprintf(yyout, "\tbx lr\n\n");
}

void MachineFunction::output()
{
    fprintf(yyout, "\t.global %s\n", this->sym_ptr->toStr().c_str() + 1);
    fprintf(yyout, "\t.type %s , %%function\n", this->sym_ptr->toStr().c_str() + 1);
    fprintf(yyout, "%s:\n", this->sym_ptr->toStr().c_str() + 1);
    // 栈帧初始化
    outputStart();
    // 更新additional args的偏移
    for (auto offset : additional_args_offset)
    {
        offset->setVal(offset->getVal() + 4 * (getSavedSRegs().size() + getSavedRRegs().size()));
        assert(!offset->isIllegalShifterOperand()); // toDo : 栈中偏移更新后非法怎么办？
    }
    // Traverse all the block in block_list to print assembly code.
    int cnt = 0;
    std::vector<MachineBlock *> empty_block_list, not_empty_block_list;
    for (auto iter : block_list)
        if (iter->getInsts().empty())
            empty_block_list.push_back(iter);
        else
            not_empty_block_list.push_back(iter);
    MachineBlock *lastBlock = nullptr;
    for (auto iter : not_empty_block_list)
    {
        if (!((*(iter->end() - 1))->isBranch()) || ((*(iter->end() - 1))->isBranch() && (*(iter->end() - 1))->getOpType() == BranchMInstruction::BL))
        {
            // 需要跳转到末尾的块最后再打印，这样就直接连起来了
            if (lastBlock == nullptr)
                lastBlock = iter;
            else
            {
                iter->output();
                cnt += iter->getInsts().size();
                // 回收栈帧
                outputEnd();
            }
        }
        else
        {
            iter->output();
            cnt += iter->getInsts().size();
        }
        if (cnt > 300)
        {
            fprintf(yyout, "\tb .LiteralPool%d\n", parent->getLtorgNo());
            fprintf(yyout, ".LTORG\n");
            parent->printBridge();
            fprintf(yyout, ".LiteralPool%d:\n", parent->getLtorgNo() - 1);
            cnt = 0;
        }
    }
    if (lastBlock != nullptr)
    {
        lastBlock->output();
        cnt += lastBlock->getInsts().size();
        if (cnt > 300)
        {
            fprintf(yyout, "\tb .LiteralPool%d\n", parent->getLtorgNo());
            fprintf(yyout, ".LTORG\n");
            parent->printBridge();
            fprintf(yyout, ".LiteralPool%d:\n", parent->getLtorgNo() - 1);
            cnt = 0;
        }
    }
    for (auto iter : empty_block_list)
        iter->output();
    outputEnd();
}

MachineFunction::~MachineFunction()
{
    auto delete_list = block_list;
    for (auto block : delete_list)
        delete block;
    parent->removeFunc(this);
}

void MachineUnit::printGlobalDecl()
{
    // print global variable declaration code;
    if (!global_var_list.empty())
        fprintf(yyout, "\t.data\n");
    for (auto var : global_var_list)
    {
        if (var->getType()->isARRAY())
        {
            if (var->isAllZero())
                fprintf(yyout, "\t.comm\t%s, %d, 4\n", var->toStr().c_str(), var->getType()->getSize());
            else
            {
                fprintf(yyout, "\t.global %s\n", var->toStr().c_str());
                fprintf(yyout, "\t.align 4\n");
                fprintf(yyout, "\t.size %s, %d\n", var->toStr().c_str(), var->getType()->getSize());
                fprintf(yyout, "%s:\n", var->toStr().c_str());
                if (var->getType()->isIntArray() || var->getType()->isConstIntArray())
                {
                    for (auto val : var->getArrVals())
                        fprintf(yyout, "\t.word %d\n", int(val));
                }
                else
                {
                    for (auto val : var->getArrVals())
                    {
                        float value = float(val);
                        fprintf(yyout, "\t.word %u\n", reinterpret_cast<unsigned &>(value));
                    }
                }
            }
        }
        else
        {
            fprintf(yyout, "\t.global %s\n", var->toStr().c_str());
            fprintf(yyout, "\t.align 4\n");
            fprintf(yyout, "\t.size %s, %d\n", var->toStr().c_str(), /*var->getType()->getSize()*/ 4);
            fprintf(yyout, "%s:\n", var->toStr().c_str());
            if (var->getType()->isInt())
                fprintf(yyout, "\t.word %d\n", int(var->getValue()));
            else
            {
                float value = float(var->getValue());
                fprintf(yyout, "\t.word %u\n", reinterpret_cast<unsigned &>(value));
            }
        }
    }
}

void MachineUnit::output()
{
    /* Hint:
     * 1. You need to print global variable/const declarition code;
     * 2. Traverse all the function in func_list to print assembly code;
     * 3. Don't forget print bridge label at the end of assembly code!! */
    fprintf(yyout, "\t.arch armv8-a\n");
    // fprintf(yyout, "\t.fpu vfpv3-d16\n");
    // fprintf(yyout, "\t.fpu neon\n");
    fprintf(yyout, "\t.arch_extension crc\n");
    fprintf(yyout, "\t.arm\n");
    printGlobalDecl();
    fprintf(yyout, "\t.text\n");
    int cnt = 0;
    for (auto iter : func_list)
    {
        iter->output();

        // literal pool
        for (auto bb : iter->getBlocks())
            cnt += bb->getInsts().size();
        if (cnt > 300)
        {
            fprintf(yyout, "\tb .LiteralPool%d\n", LtorgNo);
            fprintf(yyout, ".LTORG\n");
            printBridge();
            fprintf(yyout, ".LiteralPool%d:\n", LtorgNo - 1);
            cnt = 0;
        }
    }
    printBridge();
}

void MachineUnit::printBridge()
{
    for (auto sym_ptr : global_var_list)
    {
        fprintf(yyout, "addr_%d_%s:\n", LtorgNo, sym_ptr->toStr().c_str());
        fprintf(yyout, "\t.word %s\n", sym_ptr->toStr().c_str());
    }
    LtorgNo++;
}

void clearMachineOperands()
{
    for (auto mope : newMachineOperands)
        delete mope;
}