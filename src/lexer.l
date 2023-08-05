%option noyywrap
%option yylineno
%{
    #define YY_NO_UNPUT
    #define YY_NO_INPUT
    
    #include "parser.h"
    #include <string>
    #include <ostream>
    #include <fstream>
    extern FILE *yyin; 
    extern FILE *yyout;
    extern bool dump_tokens;
     
    int offset = 1, part = 0, lastpart = 0, order = 0;

    bool dump_first_line = true;
    void DEBUG_FOR_LEX(std::string token, std::string lexeme){
        if(dump_first_line){
            fprintf(yyout, "[DEBUG LEX]:  %12s%12s%12s%12s%12s\n","token", "lexeme", "lineno", "offset", "blockpart");
            dump_first_line = false;
        }
        fprintf(yyout, "[DEBUG LEX]:  %12s%12s%12d%12d%12d\n", token.c_str(), lexeme.c_str(), yylineno, offset, part);
        fprintf(stderr, "[DEBUG LEX]:  %12s%12s%12d%12d%12d\n", token.c_str(), lexeme.c_str(), yylineno, offset, part);
    }
%}

DECIMAL ([1-9][0-9]*|0)
OCT (0[0-7]+)
HEX (0[xX][0-9A-Fa-f]+)
ID [[:alpha:]_][[:alpha:][:digit:]_]*
DECIMAL_FLOAT ((([0-9]*[.][0-9]*([eE][+-]?[0-9]+)?)|([0-9]+[eE][+-]?[0-9]+))[fLlL]?)
HEXADECIMAL_FLOAT (0[xX](([0-9A-Fa-f]*[.][0-9A-Fa-f]*([pP][+-]?[0-9]+)?)|([0-9A-Fa-f]+[pP][+-]?[0-9]+))[fLlL]?)
EOL (\r\n|\n|\r)
WHITE [\t ]

commentblockbegin \/\*
commentelement .
commentblockend \*\/
%x BLOCKCOMMENT
LINECOMMENT (\/\/[^\n]*)
%%
{commentblockbegin} {BEGIN BLOCKCOMMENT;}
<BLOCKCOMMENT>{commentelement} {}
<BLOCKCOMMENT>{commentblockend} {BEGIN INITIAL;}

"int" {
    if(dump_tokens){
        DEBUG_FOR_LEX("INT", "int");
        offset += strlen(yytext);
    }
    return INT;
}
"float" {
    if(dump_tokens){
        DEBUG_FOR_LEX("FLOAT", "float");
        offset += strlen(yytext);
    }
    return FLOAT;
}
"void" {
    if(dump_tokens){
        DEBUG_FOR_LEX("VOID", "void");
        offset += strlen(yytext);
    }
    return VOID;
}
"const" {
    if(dump_tokens){
        DEBUG_FOR_LEX("CONST", "const");
        offset += strlen(yytext);
    }
    return CONST; 
}
"if" {
    if(dump_tokens){
        DEBUG_FOR_LEX("IF", "if");
        offset += strlen(yytext);
    }
    return IF;
}
"else" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ELSE", "else");
        offset += strlen(yytext);
    }
    return ELSE;
}
"while" {
    if(dump_tokens){
        DEBUG_FOR_LEX("WHILE", "while");
        offset += strlen(yytext);
    }
    return WHILE;
}
"break" {
    if(dump_tokens){
        DEBUG_FOR_LEX("BREAK", "break");
        offset += strlen(yytext);
    }
    return BREAK;
}
"continue" {
    if(dump_tokens){
        DEBUG_FOR_LEX("CONTINUE", "continue");
        offset += strlen(yytext);
    }
    return CONTINUE;
}
"return" {
    if(dump_tokens){
        DEBUG_FOR_LEX("RETURN", "return");
        offset += strlen(yytext);
    }
    return RETURN;
}
"=" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ASSIGN", "=");
        offset += strlen(yytext);
    }
    return ASSIGN;
}
"==" {
    if(dump_tokens){
        DEBUG_FOR_LEX("EQ", "==");
        offset += strlen(yytext);
    }
    return EQ;
}
"!=" {
    if(dump_tokens){
        DEBUG_FOR_LEX("NEQ", "!=");
        offset += strlen(yytext);
    }
    return NEQ;
}
"<" {
    if(dump_tokens){
        DEBUG_FOR_LEX("LESS", "<");
        offset += strlen(yytext);
    }
    return LESS;
}
"<=" {
    if(dump_tokens){
        DEBUG_FOR_LEX("LESSEQ", "<=");
        offset += strlen(yytext);
    }
    return LESSEQ;
}
">" {
    if(dump_tokens){
        DEBUG_FOR_LEX("GREATER", ">");
        offset += strlen(yytext);
    }
    return GREATER;
}
">=" {
    if(dump_tokens){
        DEBUG_FOR_LEX("GREATEREQ", ">=");
        offset += strlen(yytext);
    }
    return GREATEREQ;
}
"+" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ADD", "+");
        offset += strlen(yytext);
    }
    return ADD;
}
"-" {
    if(dump_tokens){
        DEBUG_FOR_LEX("SUB", "-");
        offset += strlen(yytext);
    }
    return SUB;
}
"*" {
    if(dump_tokens){
        DEBUG_FOR_LEX("MUL", "*");
        offset += strlen(yytext);
    }
    return MUL;
}
"/" {
    if(dump_tokens){
        DEBUG_FOR_LEX("DIV", "/");
        offset += strlen(yytext);
    }
    return DIV;
}
"%" {
    if(dump_tokens){
        DEBUG_FOR_LEX("MOD", "%");
        offset += strlen(yytext);
    }
    return MOD;
}
"||" {
    if(dump_tokens){
        DEBUG_FOR_LEX("OR", "||");
        offset += strlen(yytext);
    }
    return OR;
}
"&&" {
    if(dump_tokens){
        DEBUG_FOR_LEX("AND", "&&");
        offset += strlen(yytext);
    }
    return AND;
}
"!" {
    if(dump_tokens){
        DEBUG_FOR_LEX("NOT", "!");
        offset += strlen(yytext);
    }
    return NOT;
}
"," {
    if(dump_tokens){
        DEBUG_FOR_LEX("COMMA", ",");
        offset += strlen(yytext);
    }
    return COMMA;
}
";" {
    if(dump_tokens){
        DEBUG_FOR_LEX("SEMICOLON", ";");
        offset += strlen(yytext);
    }
    return SEMICOLON;
}
"(" {
    if(dump_tokens){
        DEBUG_FOR_LEX("LPAREN", "(");
        offset += strlen(yytext);
    }
    yylval.lineNo = yylineno; // for starttime & stoptime
    return LPAREN;
}
")" {
    if(dump_tokens){
        DEBUG_FOR_LEX("RPAREN", ")");
        offset += strlen(yytext);
    }
    return RPAREN;
}
"[" {
    if(dump_tokens){
        DEBUG_FOR_LEX("LBRACKET", "[");
        offset += strlen(yytext);
    }
    return LBRACKET;
}
"]" {
    if(dump_tokens){
        DEBUG_FOR_LEX("RBRACKET", "]");
        offset += strlen(yytext);
    }
    return RBRACKET; 
}
"{" {
    if(dump_tokens){
        DEBUG_FOR_LEX("LBRACE", "{");
        offset += strlen(yytext);
        order += 1;
        lastpart = part;
        part = order;
    }
    return LBRACE;
}
"}" {
    if(dump_tokens){
        DEBUG_FOR_LEX("RBRACE", "}");
        offset += strlen(yytext);
        part = lastpart;
    }
    return RBRACE;
}
"getint" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "getint");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    Type* funcType = new FunctionType(TypeSystem::intType, {});//返回类型int，无参数
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);//作用域GLOBAL(0)
    globals->install(yytext, se);
    return ID;
}
"getch" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "getch");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    Type* funcType = new FunctionType(TypeSystem::intType, {});//返回类型int，无参数
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);//作用域GLOBAL(0)
    globals->install(yytext, se);
    return ID;
}
"getfloat" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "getfloat");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    Type* funcType = new FunctionType(TypeSystem::floatType, {});//返回类型int，无参数
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);//作用域GLOBAL(0)
    globals->install(yytext, se);
    return ID;
}
"getarray" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "getarray");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    std::vector<Type*> paramsType; 
    Type* arrayType = new IntArrayType();
    ((ArrayType*)arrayType)->addDim(-1);
    paramsType.push_back(arrayType);
    Type* funcType = new FunctionType(TypeSystem::intType, paramsType);
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);
    globals->install(yytext, se);
    return ID;
}
"getfarray" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "getfarray");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    std::vector<Type*> paramsType; 
    Type* arrayType = new FloatArrayType();
    ((ArrayType*)arrayType)->addDim(-1);
    paramsType.push_back(arrayType);
    Type* funcType = new FunctionType(TypeSystem::intType, paramsType);
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);
    globals->install(yytext, se);
    return ID;
}
"putint" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "putint");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    std::vector<Type*> paramsType; 
    paramsType.push_back(TypeSystem::intType);
    Type* funcType = new FunctionType(TypeSystem::voidType, paramsType);
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);
    globals->install(yytext, se);
    return ID;
}
"putch" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "putch");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    std::vector<Type*> paramsType;
    paramsType.push_back(TypeSystem::intType);
    Type* funcType = new FunctionType(TypeSystem::voidType, paramsType);
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);
    globals->install(yytext, se);
    return ID;
}
"putfloat" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "putfloat");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    std::vector<Type*> paramsType; 
    paramsType.push_back(TypeSystem::floatType);
    Type* funcType = new FunctionType(TypeSystem::voidType, paramsType);
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);
    globals->install(yytext, se);
    return ID;
}
"putarray" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "putarray");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    std::vector<Type*> paramsType; 
    paramsType.push_back(TypeSystem::intType);
    Type* arrayType = new IntArrayType();
    ((ArrayType*)arrayType)->addDim(-1);
    paramsType.push_back(arrayType);
    Type* funcType = new FunctionType(TypeSystem::voidType, paramsType);
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);
    globals->install(yytext, se);
    return ID;
}
"putfarray" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "putfarray");
        offset += strlen(yytext);
    }
    char *lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    std::vector<Type*> paramsType;
    paramsType.push_back(TypeSystem::intType);
    Type* arrayType = new FloatArrayType();
    ((ArrayType*)arrayType)->addDim(-1);
    paramsType.push_back(arrayType);
    Type* funcType = new FunctionType(TypeSystem::voidType, paramsType);
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, yytext, 0);
    globals->install(yytext, se);
    return ID;
}
"starttime" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "starttime");
        offset += strlen(yytext);
    }
    char *lexeme = new char[6 + strlen(yytext) + 1];
    char* sysy = (char *)"_sysy_";
    strcpy(lexeme, sysy);
    strcpy(lexeme + 6, yytext);
    yylval.strtype = lexeme;
    Type* funcType = new FunctionType(TypeSystem::voidType, {TypeSystem::intType});
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, lexeme, 0);
    globals->install(lexeme, se);
    return ID;
}
"stoptime" {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", "stoptime");
        offset += strlen(yytext);
    }
    char *lexeme = new char[6 + strlen(yytext) + 1];
    char* sysy = (char*)"_sysy_";
    strcpy(lexeme, sysy);
    strcpy(lexeme + 6, yytext);
    yylval.strtype = lexeme;
    Type* funcType = new FunctionType(TypeSystem::voidType, {TypeSystem::intType});
    SymbolEntry* se = new IdentifierSymbolEntry(funcType, lexeme, 0);
    globals->install(lexeme, se);
    return ID;
}
{DECIMAL} {
    if(dump_tokens){
        DEBUG_FOR_LEX("INTEGERCONST", std::string(yytext));
        offset += strlen(yytext);
    }
    yylval.itype = atoi(yytext);
    return INTEGERCONST;
}
{DECIMAL_FLOAT} {
    if(dump_tokens){
        DEBUG_FOR_LEX("DECIMAL_FLOAT", std::string(yytext));
        offset += strlen(yytext);
    }
    yylval.ftype = atof(yytext);
    return FLOATCONST;
}
{HEXADECIMAL_FLOAT} {
    if(dump_tokens){
        DEBUG_FOR_LEX("DECIMAL_FLOAT", std::string(yytext));
        offset += strlen(yytext);
    }
    sscanf(yytext, "%f", &yylval.ftype);
    return FLOATCONST;
}
{ID} {
    if(dump_tokens){
        DEBUG_FOR_LEX("ID", std::string(yytext));
        offset += strlen(yytext);
    }
    char* lexeme = new char[strlen(yytext) + 1];
    strcpy(lexeme, yytext);
    yylval.strtype = lexeme;
    return ID;
}
{HEX} {
    char* stop;
    int dec = strtol(yytext, &stop, 16);
    if(dump_tokens){
        DEBUG_FOR_LEX("INTEGERCONST", std::to_string(dec));
        offset += strlen(yytext);
    }
    yylval.itype = dec;
    return INTEGERCONST;
}
{OCT} {
    char* stop;
    int dec = strtol(yytext, &stop, 8);
    if(dump_tokens){
        DEBUG_FOR_LEX("INTEGERCONST", std::to_string(dec));
        offset += strlen(yytext);
    }
    yylval.itype = dec;
    return INTEGERCONST;
}
<*>{EOL} {
    if(dump_tokens)
        offset = 1;
} 
{WHITE} {
    if(dump_tokens){
        if(yytext[0] == ' ')
            offset += 1;
        else
            offset += 4;
    }
}
{LINECOMMENT}

%%

