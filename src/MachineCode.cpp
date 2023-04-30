#include "MachineCode.h"
#include <queue>
#include <sstream>
#include <iomanip>
#include <set>
extern FILE *yyout;

static std::vector<MachineOperand *> newMachineOperands = std::vector<MachineOperand *>(); // 用来回收new出来的SymbolEntry

extern bool mem2reg;

static int cnt = 0; // 当前已打印指令数量，每次打印LiteralPool都会清零

// https://developer.arm.com/documentation/ka001136/latest
static std::set<float> legal_float_imm = std::set<float>({
    2.00000000,
    2.12500000,
    2.25000000,
    2.37500000,
    2.50000000,
    2.62500000,
    2.75000000,
    2.87500000,
    3.00000000,
    3.12500000,
    3.25000000,
    3.37500000,
    3.50000000,
    3.62500000,
    3.75000000,
    3.87500000,
    4.00000000,
    4.25000000,
    4.50000000,
    4.75000000,
    5.00000000,
    5.25000000,
    5.50000000,
    5.75000000,
    6.00000000,
    6.25000000,
    6.50000000,
    6.75000000,
    7.00000000,
    7.25000000,
    7.50000000,
    7.75000000,
    8.00000000,
    8.50000000,
    9.00000000,
    9.50000000,
    10.00000000,
    10.50000000,
    11.00000000,
    11.50000000,
    12.00000000,
    12.50000000,
    13.00000000,
    13.50000000,
    14.00000000,
    14.50000000,
    15.00000000,
    15.50000000,
    16.00000000,
    17.00000000,
    18.00000000,
    19.00000000,
    20.00000000,
    21.00000000,
    22.00000000,
    23.00000000,
    24.00000000,
    25.00000000,
    26.00000000,
    27.00000000,
    28.00000000,
    29.00000000,
    30.00000000,
    31.00000000,
    0.12500000,
    0.13281250,
    0.14062500,
    0.14843750,
    0.15625000,
    0.16406250,
    0.17187500,
    0.17968750,
    0.18750000,
    0.19531250,
    0.20312500,
    0.21093750,
    0.21875000,
    0.22656250,
    0.23437500,
    0.24218750,
    0.25000000,
    0.26562500,
    0.28125000,
    0.29687500,
    0.31250000,
    0.32812500,
    0.34375000,
    0.35937500,
    0.37500000,
    0.39062500,
    0.40625000,
    0.42187500,
    0.43750000,
    0.45312500,
    0.46875000,
    0.48437500,
    0.50000000,
    0.53125000,
    0.56250000,
    0.59375000,
    0.62500000,
    0.65625000,
    0.68750000,
    0.71875000,
    0.75000000,
    0.78125000,
    0.81250000,
    0.84375000,
    0.87500000,
    0.90625000,
    0.93750000,
    0.96875000,
    1.00000000,
    1.06250000,
    1.12500000,
    1.18750000,
    1.25000000,
    1.31250000,
    1.37500000,
    1.43750000,
    1.50000000,
    1.56250000,
    1.62500000,
    1.68750000,
    1.75000000,
    1.81250000,
    1.87500000,
    1.93750000,
    -2.00000000,
    -2.12500000,
    -2.25000000,
    -2.37500000,
    -2.50000000,
    -2.62500000,
    -2.75000000,
    -2.87500000,
    -3.00000000,
    -3.12500000,
    -3.25000000,
    -3.37500000,
    -3.50000000,
    -3.62500000,
    -3.75000000,
    -3.87500000,
    -4.00000000,
    -4.25000000,
    -4.50000000,
    -4.75000000,
    -5.00000000,
    -5.25000000,
    -5.50000000,
    -5.75000000,
    -6.00000000,
    -6.25000000,
    -6.50000000,
    -6.75000000,
    -7.00000000,
    -7.25000000,
    -7.50000000,
    -7.75000000,
    -8.00000000,
    -8.50000000,
    -9.00000000,
    -9.50000000,
    -10.00000000,
    -10.50000000,
    -11.00000000,
    -11.50000000,
    -12.00000000,
    -12.50000000,
    -13.00000000,
    -13.50000000,
    -14.00000000,
    -14.50000000,
    -15.00000000,
    -15.50000000,
    -16.00000000,
    -17.00000000,
    -18.00000000,
    -19.00000000,
    -20.00000000,
    -21.00000000,
    -22.00000000,
    -23.00000000,
    -24.00000000,
    -25.00000000,
    -26.00000000,
    -27.00000000,
    -28.00000000,
    -29.00000000,
    -30.00000000,
    -31.00000000,
    -0.12500000,
    -0.13281250,
    -0.14062500,
    -0.14843750,
    -0.15625000,
    -0.16406250,
    -0.17187500,
    -0.17968750,
    -0.18750000,
    -0.19531250,
    -0.20312500,
    -0.21093750,
    -0.21875000,
    -0.22656250,
    -0.23437500,
    -0.24218750,
    -0.25000000,
    -0.26562500,
    -0.28125000,
    -0.29687500,
    -0.31250000,
    -0.32812500,
    -0.34375000,
    -0.35937500,
    -0.37500000,
    -0.39062500,
    -0.40625000,
    -0.42187500,
    -0.43750000,
    -0.45312500,
    -0.46875000,
    -0.48437500,
    -0.50000000,
    -0.53125000,
    -0.56250000,
    -0.59375000,
    -0.62500000,
    -0.65625000,
    -0.68750000,
    -0.71875000,
    -0.75000000,
    -0.78125000,
    -0.81250000,
    -0.84375000,
    -0.87500000,
    -0.90625000,
    -0.93750000,
    -0.96875000,
    -1.00000000,
    -1.06250000,
    -1.12500000,
    -1.18750000,
    -1.25000000,
    -1.31250000,
    -1.37500000,
    -1.43750000,
    -1.50000000,
    -1.56250000,
    -1.62500000,
    -1.68750000,
    -1.75000000,
    -1.81250000,
    -1.87500000,
    -1.93750000,
});

union VAL
{
    unsigned unsigned_val;
    signed signed_val;
    float float_val;
};

// 合法的第二操作数循环移位偶数位后可以用8bit表示
bool isShifterOperandVal(unsigned bin_val)
{
    int i = 0;
    while (i < 32)
    {
        unsigned shift_val = ((bin_val) >> i) | ((bin_val) << (32 - i)); // 循环右移i位
        if ((shift_val & 0xFFFFFF00) == 0x00000000)
            return true;
        i = i + 2;
    }
    return false;
}

// TODO
bool isSignedShifterOperandVal(signed signed_val)
{
    VAL val;
    val.signed_val = signed_val;
    // return signed_val >= 0 ? isShifterOperandVal(val.unsigned_val) : signed_val >= -255;
    return signed_val >= 0 ? isShifterOperandVal(val.unsigned_val) : isShifterOperandVal(~val.unsigned_val);
}

bool is_Legal_VMOV_FloatImm(float float_val)
{
    return legal_float_imm.find(float_val) != legal_float_imm.end();
}

MachineOperand::MachineOperand(int tp, double val, Type *valType)
{
    this->type = tp;
    if (tp == MachineOperand::IMM)
    {
        this->val = val;
        this->reg_no = -1;
    }
    else
    {
        assert(tp == MachineOperand::REG || tp == MachineOperand::VREG);
        this->val = 0;
        this->reg_no = (int)val;
    }
    this->label = "";
    // 约定MachineOperand的valType是int/float/bool
    assert(!valType->isARRAY());
    assert(!valType->isPTR());
    this->valType = tp == MachineOperand::IMM ? Var2Const(valType) : valType;
    this->parent = nullptr;
    newMachineOperands.push_back(this);
}

MachineOperand::MachineOperand(std::string label)
{
    this->type = MachineOperand::LABEL;
    this->val = 0;
    this->reg_no = -1;
    this->label = label;
    this->valType = TypeSystem::intType;
    this->parent = nullptr;
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

void MachineOperand::output()
{
    /* HINT：print operand
     * Example:
     * immediate num 1 -> print #1;
     * register 1 -> print r1;
     * label addr_a -> print addr_a; */
    fprintf(yyout, "%s", this->toStr().c_str());
}

std::string MachineOperand::toStr()
{
    std::string operandstr;
    switch (this->type)
    {
    case IMM:
    {
        if (valType->isFloat())
        {
            VAL val;
            val.float_val = (float)(this->val);
            operandstr = "#" + std::to_string(val.unsigned_val);
        }
        else
        {
            assert(valType->isInt());
            operandstr = "#" + std::to_string((int)this->val);
        }
        break;
    }
    case VREG:
    {
        assert(valType->isInt() || valType->isFloat());
        std::string str = valType->isFloat() ? "vs" : "vr";
        operandstr = str + std::to_string(this->reg_no);
        break;
    }
    case REG:
    {
        if (valType->isFloat())
        {
            switch (reg_no)
            {
            // case 32:
            //     operandstr = "FPSCR";
            //     break;
            default:
                operandstr = "s" + std::to_string(reg_no);
                break;
            }
        }
        else
        {
            assert(valType->isInt());
            {
                switch (reg_no)
                {
                case 11:
                    operandstr = "fp";
                    break;
                case 13:
                    operandstr = "sp";
                    break;
                case 14:
                    operandstr = "lr";
                    break;
                case 15:
                    operandstr = "pc";
                    break;
                default:
                    operandstr = "r" + std::to_string(reg_no);
                    break;
                }
            }
        }
        break;
    }
    case LABEL:
    {
        if (this->label.substr(0, 2) == ".L")
            operandstr = this->label;
        else if (this->label.substr(0, 1) == "@")
            operandstr = this->label.substr(1);
        else
            operandstr = "addr_" + std::to_string(parent->getParent()->getParent()->getParent()->getLtorgNo()) + "_" + this->label;
        break;
    }
    default:
        assert(0);
    }
    return operandstr;
}

bool MachineOperand::isIllegalShifterOperand()
{
    assert(this->isImm());
    if (valType->isFloat())
        return true;
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

bool MachineInstruction::isAdd() const
{
    return type == BINARY && op == BinaryMInstruction::ADD;
}

bool MachineInstruction::isSub() const
{
    return type == BINARY && op == BinaryMInstruction::SUB;
}

bool MachineInstruction::isRsb() const
{
    return type == BINARY && op == BinaryMInstruction::RSB;
}

bool MachineInstruction::isMul() const
{
    return type == BINARY && op == BinaryMInstruction::MUL;
}

bool MachineInstruction::isDiv() const
{
    return type == BINARY && op == BinaryMInstruction::DIV;
}

bool MachineInstruction::isMov() const
{
    return type == MOV && op == MovMInstruction::MOV;
}

bool MachineInstruction::isVmov() const
{
    return type == MOV && op == MovMInstruction::VMOV;
}

bool MachineInstruction::isBL() const
{
    return type == BRANCH && op == BranchMInstruction::BL;
}

bool MachineInstruction::isBranch() const
{
    return type == BRANCH;
}

bool MachineInstruction::isCondMov() const
{
    return type == MOV && (op == MovMInstruction::MOV || op == MovMInstruction::VMOV) && cond != NONE;
}

bool MachineInstruction::isSmull() const
{
    return type == SMULL;
}

bool MachineInstruction::isCritical() const
{
    if (isDummy() || type == STACK || type == BRANCH)
        return true;
    for (auto def : def_list)
    {
        if (def->getReg() == 13 && def->isReg())
            return true;
    }
    return false;
}

DummyMInstruction::DummyMInstruction(
    MachineBlock *p,
    std::vector<MachineOperand *> defs, std::vector<MachineOperand *> uses,
    int cond)
{
    this->parent = p;
    this->type = MachineInstruction::DUMMY;
    this->op = -1;
    this->cond = cond;
    for (auto def : defs)
    {
        this->def_list.push_back(def);
        def->setParent(this);
    }
    for (auto use : uses)
    {
        this->use_list.push_back(use);
        use->setParent(this);
    }
}

// 调试用，后面可以删了
void DummyMInstruction::output()
{
    fprintf(yyout, "\t@ dummy ");
    if (!def_list.empty())
    {
        fprintf(yyout, "\tdefs:{ ");
        for (auto def : def_list)
        {
            def->output();
            fprintf(yyout, " ");
        }
        fprintf(yyout, "}");
    }
    if (!use_list.empty())
    {
        fprintf(yyout, "\tuses:{ ");
        for (auto use : use_list)
        {
            use->output();
            fprintf(yyout, " ");
        }
        fprintf(yyout, "}");
    }
    fprintf(yyout, "\n");
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
    if (this->use_list[1]->isImm() && this->use_list[1]->getVal() == 0)
    {
        if (this->op == BinaryMInstruction::RSB)
        {
            if (def_list[0]->getValType()->isFloat())
                fprintf(yyout, "\tvneg.f32");
            else
                fprintf(yyout, "\tneg");
            printCond();
            fprintf(yyout, " ");
            this->def_list[0]->output();
            fprintf(yyout, ", ");
            this->use_list[0]->output();
            fprintf(yyout, "\n");
            return;
        }
        else if (this->op == BinaryMInstruction::ADD || this->op == BinaryMInstruction::SUB)
        {
            if (*def_list[0] == *use_list[0])
                return;
            if (def_list[0]->getValType()->isFloat())
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
        else
            assert(0);
    }
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
            assert(0);
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
        case BinaryMInstruction::AND:
            fprintf(yyout, "\tand");
            break;
        case BinaryMInstruction::RSB:
            fprintf(yyout, "\trsb");
            break;
        default:
            assert(0);
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
    if ((this->use_list.size() == 1) && this->use_list[0]->isImm())
    {
        if (this->def_list[0]->getValType()->isInt())
        {
            unsigned temp;
            if (this->use_list[0]->getValType()->isInt())
            {
                VAL val;
                val.signed_val = (int)this->use_list[0]->getVal();
                temp = val.unsigned_val;
            }
            else
            {
                assert(this->use_list[0]->getValType()->isFloat());
                VAL val;
                val.float_val = (float)this->use_list[0]->getVal();
                temp = val.unsigned_val;
            }
            if (isShifterOperandVal(temp))
            {
                fprintf(yyout, "\tmov");
                printCond();
                fprintf(yyout, " ");
                this->def_list[0]->output();
                fprintf(yyout, ", #%u\n", temp);
                return;
            }
            else if ((this->use_list[0]->getValType()->isInt() && isShifterOperandVal(~temp)))
            {
                fprintf(yyout, "\tmvn");
                printCond();
                fprintf(yyout, " ");
                this->def_list[0]->output();
                fprintf(yyout, ", #%u\n", ~temp);
                return;
            }
            else
            {
                unsigned high = (temp & 0xFFFF0000) >> 16;
                unsigned low = temp & 0x0000FFFF;
                if (isShifterOperandVal(high) && isShifterOperandVal(low))
                {
                    if (low)
                    {
                        fprintf(yyout, "\tmovw");
                        printCond();
                        fprintf(yyout, " ");
                        this->def_list[0]->output();
                        fprintf(yyout, ", #%u\n", low);
                    }

                    if (high)
                    {
                        fprintf(yyout, "\tmovt");
                        printCond();
                        fprintf(yyout, " ");
                        this->def_list[0]->output();
                        fprintf(yyout, ", #%u\n", high);
                    }
                    return;
                }
            }
        }
        else
        {
            assert(this->def_list[0]->getValType()->isFloat());
            assert(this->use_list[0]->getValType()->isFloat());
        }
    }

    if (this->def_list[0]->getValType()->isFloat())
        fprintf(yyout, "\tvldr.32");
    // else if (this->def_list[0]->getValType()->isBool())
    //     fprintf(yyout, "\tldrb");
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
            VAL val;
            val.float_val = (float)(this->use_list[0]->getVal());
            fprintf(yyout, "=%u\n", val.unsigned_val);
        }
        else
            fprintf(yyout, "=%d\n", (int)this->use_list[0]->getVal());
        return;
    }

    // Load address
    if (this->use_list[0]->isReg() || this->use_list[0]->isVReg())
        fprintf(yyout, "[");

    this->use_list[0]->output();
    if (this->use_list.size() > 1 && !(this->use_list[1]->isImm() && this->use_list[1]->getVal() == 0))
    {
        if (this->use_list[1]->isImm())
            assert(((int)this->use_list[1]->getVal() % 4) == 0);
        if (this->def_list[0]->getValType()->isFloat())
            assert(this->use_list[1]->isImm()); // VFP好像不支持用寄存器做相对偏移?
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
        if (this->use_list[0]->getValType()->isFloat())
            assert(this->use_list[2]->isImm()); // VFP好像不支持用寄存器做相对偏移?
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
        assert(op == MOVASR || op == MOVLSL || op == MOVLSR);
        assert(shifter->isImm() && shifter->getValType()->isInt());
        this->use_list.push_back(shifter);
        shifter->setParent(this);
    }
}

void MovMInstruction::output()
{
    if ((*def_list[0]) == (*use_list[0]))
    {
        if (use_list.size() == 1)
            // return;
            fprintf(yyout, "\t@");
        else
        {
            assert(use_list.size() == 2);
            if ((int)this->use_list[1]->getVal())
            {
                switch (this->op)
                {
                case MovMInstruction::MOVLSL:
                    fprintf(yyout, "\tlsl");
                    break;
                case MovMInstruction::MOVLSR:
                    fprintf(yyout, "\tlsr");
                    break;
                case MovMInstruction::MOVASR:
                    fprintf(yyout, "\tasr");
                    break;
                default:
                    assert(0);
                }
                printCond();
                fprintf(yyout, " ");
                this->def_list[0]->output();
                fprintf(yyout, ", ");
                this->use_list[0]->output();
                fprintf(yyout, ", #%d\n", (int)this->use_list[1]->getVal());
            }
            return;
        }
    }
    switch (this->op)
    {
    case MovMInstruction::MOV:
    case MovMInstruction::MOVLSL:
    case MovMInstruction::MOVLSR:
    case MovMInstruction::MOVASR:
    {
        // if (use_list[0]->getValType()->isBool())
        //     fprintf(yyout, "\tmovw"); // move byte指令呢?
        // else
        // {
        fprintf(yyout, "\tmov"); // 为了能在coalesce时消去uxtb，这里不单独讨论movw的情况了
        // }
        break;
    }
        // case MovMInstruction::MVN:
        //     fprintf(yyout, "\tmvn");
        //     break;
        // case MovMInstruction::MOVT:
        //     fprintf(yyout, "\tmovt");
        //     break;
    case MovMInstruction::VMOV:
    {
        fprintf(yyout, "\tvmov.f32"); // todo:到底用哪个
        // fprintf(yyout, "\tvmov");
        break;
    }
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
        if ((int)this->use_list[1]->getVal())
            fprintf(yyout, ", LSL #%d", (int)this->use_list[1]->getVal());
        break;

    case MovMInstruction::MOVLSR:
        if ((int)this->use_list[1]->getVal())
            fprintf(yyout, ", LSR #%d", (int)this->use_list[1]->getVal());
        break;

    case MovMInstruction::MOVASR:
        if ((int)this->use_list[1]->getVal())
            fprintf(yyout, ", ASR #%d", (int)this->use_list[1]->getVal());
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
    if (def_list.size() > 1)
    {
        fprintf(yyout, "\t@ defs:{ ");
        for (size_t i = 1; i != def_list.size(); i++)
        {
            def_list[i]->output();
            fprintf(yyout, " ");
        }
        fprintf(yyout, "}");
    }
    if (!use_list.empty())
    {
        if (def_list.size() == 1)
            fprintf(yyout, "\t@ uses:{ ");
        else
            fprintf(yyout, "\tuses:{ ");
        for (auto use : use_list)
        {
            use->output();
            fprintf(yyout, " ");
        }
        fprintf(yyout, "}");
    }
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
    if (this->use_list.empty())
        return;
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

SmullMInstruction::SmullMInstruction(MachineBlock *p,
                                     MachineOperand *dst1,
                                     MachineOperand *dst2,
                                     MachineOperand *src1,
                                     MachineOperand *src2,
                                     int cond)
{
    this->parent = p;
    this->type = MachineInstruction::SMULL;
    this->cond = cond;
    this->def_list.push_back(dst1);
    this->def_list.push_back(dst2);
    this->use_list.push_back(src1);
    this->use_list.push_back(src2);
    dst1->setParent(this);
    dst2->setParent(this);
    src1->setParent(this);
    src2->setParent(this);
}

void SmullMInstruction::output()
{
    fprintf(yyout, "\tsmull ");
    this->def_list[0]->output();
    fprintf(yyout, ", ");
    this->def_list[1]->output();
    fprintf(yyout, ", ");
    this->use_list[0]->output();
    fprintf(yyout, ", ");
    this->use_list[1]->output();
    fprintf(yyout, "\n");
}

MLASMInstruction::MLASMInstruction(MachineBlock *p,
                                   int op,
                                   MachineOperand *dst,
                                   MachineOperand *src1,
                                   MachineOperand *src2,
                                   MachineOperand *src3)
{
    this->parent = p;
    this->type = MachineInstruction::MLAS;
    this->op = op;
    this->def_list.push_back(dst);
    this->use_list.push_back(src1);
    this->use_list.push_back(src2);
    this->use_list.push_back(src3);
    dst->setParent(this);
    src1->setParent(this);
    src2->setParent(this);
    src3->setParent(this);
    // dst->setDef(this);
}
void MLASMInstruction::output()
{
    switch (this->op)
    {
    case MLASMInstruction::MLA:
        fprintf(yyout, "\tmla ");
        break;
    case MLASMInstruction::MLS:
        fprintf(yyout, "\tmls ");
        break;
    default:
        break;
    }

    printCond();
    this->def_list[0]->output();
    fprintf(yyout, ", ");
    this->use_list[0]->output();
    fprintf(yyout, ", ");
    this->use_list[1]->output();
    fprintf(yyout, ", ");
    this->use_list[2]->output();
    fprintf(yyout, "\n");
}


VMLASMInstruction::VMLASMInstruction(MachineBlock* p,
                                   int op,
                                   MachineOperand* dst,
                                   MachineOperand* src1,
                                   MachineOperand* src2) {
    this->parent = p;
    this->type = MachineInstruction::VMLAS;
    this->op = op;
    this->use_list.push_back(dst);
    this->use_list.push_back(src1);
    this->use_list.push_back(src2);
    dst->setParent(this);
    src1->setParent(this);
    src2->setParent(this);
}
void VMLASMInstruction::output() {
    switch (this->op) {
        case VMLASMInstruction::VMLA:
            fprintf(yyout, "\tvmla.f32 ");
            break;
        case VMLASMInstruction::VMLS:
            fprintf(yyout, "\tvmls.f32 ");
            break;
        default:
            break;
    }
    printCond();
    this->use_list[0]->output();
    fprintf(yyout, ", ");
    this->use_list[1]->output();
    fprintf(yyout, ", ");
    this->use_list[2]->output();
    fprintf(yyout, "\n");
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
    // ToDo:有些浮点字面常量可以直接vldr到s寄存器
    if (imm->getValType()->isFloat())
    {
        if (is_Legal_VMOV_FloatImm((float)imm->getVal()))
        {
            MachineOperand *internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);
            this->insertInst(new MovMInstruction(this, MovMInstruction::VMOV, internal_reg, imm));
            return new MachineOperand(*internal_reg);
        }
        MachineOperand *internal_reg1 = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::intType);
        this->insertInst(new LoadMInstruction(this, internal_reg1, imm));
        MachineOperand *internal_reg2 = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);
        internal_reg1 = new MachineOperand(*internal_reg1);
        this->insertInst(new MovMInstruction(this, MovMInstruction::VMOV, internal_reg2, internal_reg1));
        return new MachineOperand(*internal_reg2);
        // MachineOperand *internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);
        // this->insertInst(new LoadMInstruction(this, internal_reg, imm));
        // return new MachineOperand(*internal_reg);
    }
    assert(imm->getValType()->isInt());
    MachineOperand *internal_reg = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::intType);
    this->insertInst(new LoadMInstruction(this, internal_reg, imm));
    return new MachineOperand(*internal_reg);
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
    for (auto iter : inst_list)
    {
        iter->output();
        cnt++;
        if (cnt > 750)
        {
            fprintf(yyout, "\tb .LiteralPool%d_end\n", parent->getParent()->getLtorgNo());
            fprintf(yyout, ".LTORG\n");
            parent->getParent()->printBridge();
            fprintf(yyout, ".LiteralPool%d_end:\n", parent->getParent()->getLtorgNo() - 1);
            cnt = 0;
        }
    }
}

MachineFunction::MachineFunction(MachineUnit *p, SymbolEntry *sym_ptr)
{
    this->parent = p;
    this->sym_ptr = sym_ptr;
    this->stack_size = 0;
    this->largeStack = false;
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
    if (saved_rregs.size() == 1 && stack_size >= 1028)
        saved_rregs.insert(1);
    // Save callee saved int registers
    auto rregs = getSavedRRegs();
    auto inst = new StackMInstruction(nullptr, StackMInstruction::PUSH, rregs);
    inst->output();
    delete inst;
    // Save callee saved float registers
    auto sregs = getSavedSRegs();
    inst = new StackMInstruction(nullptr, StackMInstruction::PUSH, sregs);
    inst->output();
    delete inst;
    // fp = sp
    fprintf(yyout, "\tmov fp, sp\n"); // to do：判断一下，没用过fp的话这句就省了
    if (dynamic_cast<IdentifierSymbolEntry *>(sym_ptr)->need8BytesAligned() &&
        (4 * (rregs.size() + sregs.size() + std::max(0, (int)dynamic_cast<FunctionType *>(sym_ptr->getType())->getParamsType().size() - 4)) + stack_size) % 8)
        stack_size += 4;
    // Allocate stack space for local variable
    if (stack_size)
    {
        if (!isShifterOperandVal(stack_size))
        {
            auto reg_no = (*saved_rregs.begin());
            if (reg_no == 11)
            {
                assert(saved_rregs.size() >= 2);
                reg_no = (*saved_rregs.rbegin());
            }
            auto inst = new LoadMInstruction(nullptr, new MachineOperand(MachineOperand::REG, reg_no), new MachineOperand(MachineOperand::IMM, stack_size));
            inst->output(); // fprintf(yyout, "\tldr r%d, =%d\n", reg_no, stack_size);
            delete inst;
            fprintf(yyout, "\tsub sp, sp, %s\n", (new MachineOperand(MachineOperand::REG, reg_no))->toStr().c_str());
        }
        else
            fprintf(yyout, "\tsub sp, sp, #%d\n", stack_size);
    }
}

void MachineFunction::outputEnd()
{
    // recycle stack space
    if (stack_size)
    {
        if (hasLargeStack())
            fprintf(yyout, "\tmov sp, fp\n");
        else
        {
            if (!isShifterOperandVal(stack_size))
            {
                auto reg_no = (*saved_rregs.begin());
                auto inst = new LoadMInstruction(nullptr, new MachineOperand(MachineOperand::REG, reg_no), new MachineOperand(MachineOperand::IMM, stack_size));
                inst->output(); // fprintf(yyout, "\tldr r%d, =%d\n", reg_no, stack_size);
                delete inst;
                fprintf(yyout, "\tadd sp, sp, %s\n", (new MachineOperand(MachineOperand::REG, reg_no))->toStr().c_str());
            }
            else
                fprintf(yyout, "\tadd sp, sp, #%d\n", stack_size);
        }
    }
    // Restore saved registers
    auto inst = new StackMInstruction(nullptr, StackMInstruction::POP, getSavedSRegs());
    inst->output();
    delete inst;
    inst = new StackMInstruction(nullptr, StackMInstruction::POP, getSavedRRegs());
    inst->output();
    delete inst;
    // Generate bx instruction
    fprintf(yyout, "\tbx lr\n\n");
}

void MachineFunction::output()
{
    fprintf(yyout, "\t.global %s\n", this->sym_ptr->toStr().c_str() + 1);
    fprintf(yyout, "\t.type %s , %%function\n", this->sym_ptr->toStr().c_str() + 1);
    fprintf(yyout, "%s:\n", this->sym_ptr->toStr().c_str() + 1);
    // 插入栈帧初始化代码
    outputStart();
    // 更新additional args的偏移
    for (auto offset : additional_args_offset)
        offset->setVal(offset->getVal() + 4 * (getSavedSRegs().size() + getSavedRRegs().size()));
    // Traverse all the block in block_list to print assembly code.
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
                // 回收栈帧
                outputEnd();
                if (cnt > 300)
                {
                    fprintf(yyout, ".LTORG\n");
                    this->getParent()->printBridge();
                    cnt = 0;
                }
            }
        }
        else
        {
            iter->output();
            if (cnt > 300)
            {
                fprintf(yyout, ".LTORG\n");
                this->getParent()->printBridge();
                cnt = 0;
            }
        }
    }
    if (lastBlock != nullptr)
        lastBlock->output();
    for (auto iter : empty_block_list)
        iter->output();
    outputEnd();
}

void MachineFunction::computeDom()
{
    // Vertex-removal Algorithm, O(n^2)
    for (auto bb : getBlocks())
        bb->getSDoms() = std::set<MachineBlock *>();
    std::set<MachineBlock *> all_bbs(getBlocks().begin(), getBlocks().end());
    for (auto removed_bb : getBlocks())
    {
        std::set<MachineBlock *> visited;
        std::queue<MachineBlock *> q;
        std::map<MachineBlock *, bool> is_visited;
        for (auto bb : getBlocks())
            is_visited[bb] = false;
        if (getEntry() != removed_bb)
        {
            visited.insert(getEntry());
            is_visited[getEntry()] = true;
            q.push(getEntry());
            while (!q.empty())
            {
                MachineBlock *cur = q.front();
                q.pop();
                for (auto succ : cur->getSuccs())
                    if (succ != removed_bb && !is_visited[succ])
                    {
                        q.push(succ);
                        visited.insert(succ);
                        is_visited[succ] = true;
                    }
            }
        }
        std::set<MachineBlock *> not_visited;
        set_difference(all_bbs.begin(), all_bbs.end(), visited.begin(), visited.end(), inserter(not_visited, not_visited.end()));
        for (auto bb : not_visited)
        {
            if (bb != removed_bb)
                bb->getSDoms().insert(removed_bb); // strictly dominators
        }
    }
    // immediate dominator ：严格支配 bb，且不严格支配任何严格支配 bb 的节点的节点
    std::set<MachineBlock *> temp_IDoms;
    for (auto bb : getBlocks())
    {
        temp_IDoms = bb->getSDoms();
        for (auto sdom : bb->getSDoms())
        {
            std::set<MachineBlock *> diff_set;
            set_difference(temp_IDoms.begin(), temp_IDoms.end(), sdom->getSDoms().begin(), sdom->getSDoms().end(), inserter(diff_set, diff_set.end()));
            temp_IDoms = diff_set;
        }
        assert(temp_IDoms.size() == 1 || (bb == getEntry() && temp_IDoms.size() == 0));
        if (bb != getEntry())
            bb->getIDom() = *temp_IDoms.begin();
    }
    // for (auto bb : getBlocks())
    //     if (bb != getEntry())
    //         fprintf(stderr, "IDom[.L%d] = .L%d\n", bb->getNo(), bb->getIDom()->getNo());
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
            if (var->getNonZeroCnt() == 0)
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
                        VAL value;
                        value.float_val = float(val);
                        fprintf(yyout, "\t.word %u\n", value.unsigned_val);
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
                VAL value;
                value.float_val = float(var->getValue());
                fprintf(yyout, "\t.word %u\n", value.unsigned_val);
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
    for (auto iter : func_list)
    {
        iter->output();
        if (cnt > 300)
        {
            fprintf(yyout, ".LTORG\n");
            this->printBridge();
            cnt = 0;
        }
    }
    if (cnt)
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
    for (auto &mope : newMachineOperands)
    {
        delete mope;
        mope = nullptr;
    }
}