#include <iostream>
#include <string.h>
#include <unistd.h>
#include "Ast.h"
#include "Unit.h"
#include "MachineCode.h"
#include "LinearScan.h"
#include "SimplifyCFG.h"
#include "AutoInline.h"
#include "Mem2Reg.h"
#include "ElimPHI.h"
#include "LiveVariableAnalysis.h"
#include "StrengthReduction.h"
#include "ComSubExprElim.h"
#include "DeadCodeElim.h"
#include "SparseCondConstProp.h"
#include "PeepholeOptimization.h"
#include "gvnpre.h"

Ast ast;
Unit *unit = new Unit();
MachineUnit *mUnit = new MachineUnit();
extern FILE *yyin;
extern FILE *yyout;
extern void clearSymbolEntries();
extern void clearTypes();
extern void clearMachineOperands();
int yyparse();

char outfile[256] = "a.out";
bool dump_tokens = false;
bool dump_ast = false;
bool dump_ir = false;
bool dump_asm = false;
bool optimize = false;

int main(int argc, char *argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "Siato:O::")) != -1)
    {
        switch (opt)
        {
        case 'o':
            strcpy(outfile, optarg);
            break;
        case 'a':
            dump_ast = true;
            break;
        case 't':
            dump_tokens = true;
            break;
        case 'i':
            dump_ir = true;
            break;
        case 'S':
            dump_asm = true;
            break;
        case 'O':
            optimize = true;
            break;
        default:
            fprintf(stderr, "Usage: %s [-o outfile] infile\n", argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }
    if (optind >= argc)
    {
        fprintf(stderr, "no input file\n");
        exit(EXIT_FAILURE);
    }
    if (!(yyin = fopen(argv[optind], "r")))
    {
        fprintf(stderr, "%s: No such file or directory\nno input file\n", argv[optind]);
        exit(EXIT_FAILURE);
    }
    if (!(yyout = fopen(outfile, "w")))
    {
        fprintf(stderr, "%s: fail to open output file\n", outfile);
        exit(EXIT_FAILURE);
    }
    yyparse();
    fprintf(stderr, "ast built\n");
    if (dump_ast)
    {
        ast.output();
        fprintf(stderr, "ast output ok\n");
    }
    // ast.typeCheck();
    ast.genCode(unit);
    fprintf(stderr, "ir generated\n");
    // optimize = false;
    // yyout = stderr;
    if (dump_ir && !optimize)
    {
        unit->output();
        fprintf(stderr, "ir output ok\n");
    }
    if (optimize)
    {
        for (int i = 0; i < 4; i++)
        {
            // AutoInliner autoinliner(unit);
            // autoinliner.pass();  // 函数自动内联
            Mem2Reg m2r(unit);
            m2r.pass();
            // TODO:其它中间代码优化
            // GVNPRE gvnpre(unit);
            // gvnpre.pass(); // 部分冗余消除&循环不变外提
            // 代数化简
            SparseCondConstProp sccp(unit);
            sccp.pass(); // 常量传播
            ComSubExprElim cse(unit);
            cse.pass3(); // 公共子表达式消除
            // 访存优化
            DeadCodeElim dce(unit);
            dce.pass(); // 死代码删除
        }
        fprintf(stderr, "opt ir generated\n");
        if (dump_ir)
        {
            unit->output();
            fprintf(stderr, "opt ir output ok\n");
        }
        ElimPHI ep(unit);
        ep.pass();
    }
    if (dump_asm)
    {
        unit->genMachineCode(mUnit);
        if (optimize)
        {
            // TODO: 汇编代码优化
            MachineDeadCodeElim mdce(mUnit);

            ComSubExprElimASM cseasm(mUnit);
            cseasm.pass(); // 后端cse

            StrengthReduction sr(mUnit);
            sr.pass();        // 强度削弱
            mdce.pass(false); // 死代码消除

            // cseasm.pass();

            PeepholeOptimization ph(mUnit);
            ph.pass(); // 窥孔优化

            mdce.pass(true); // 死代码消除
            // 指令调度
        }
        LinearScan linearScan(mUnit);
        linearScan.pass();
        if (optimize)
        {
            // TODO: 汇编代码优化
            ComSubExprElimASM cseasm(mUnit);
            cseasm.pass(); // 公共子表达式删除
            PeepholeOptimization ph(mUnit);
            ph.pass(); // 窥孔优化
            // 控制流优化
            // 相对fp偏移非法但相对sp偏移不非法，转化一下
            MachineDeadCodeElim mdce(mUnit);
            mdce.pass(true); // 死代码消除
        }
        fprintf(stderr, "asm generated\n");
        mUnit->output();
        fprintf(stderr, "asm output ok\n");
    }
    delete mUnit;
    delete unit;
    clearSymbolEntries();
    clearTypes();
    clearMachineOperands();
    return 0;
}
