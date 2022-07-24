/* 
 * @copyright (c) 2008, Hedspi, Hanoi University of Technology
 * @author Huu-Duc Nguyen
 * @version 1.0
 */
#include <stdio.h>
#include <stdlib.h>

#include "reader.h"
#include "scanner.h"
#include "parser.h"
#include "semantics.h"
#include "error.h"
#include "debug.h"
#include "codegen.h"

Token *currentToken;
Token *lookAhead;

Instruction *breakInstruction[100];
int breakInstructionCount = 0;

extern Type* intType;
extern Type* charType;
extern SymTab* symtab;

extern CodeBlock *codeBlock;

void scan(void) {
  Token* tmp = currentToken;
  currentToken = lookAhead;
  lookAhead = getValidToken();
  free(tmp);
}

void eat(TokenType tokenType) {
  if (lookAhead->tokenType == tokenType) {
    //    printToken(lookAhead);
    scan();
  } else missingToken(tokenType, lookAhead->lineNo, lookAhead->colNo);
}

void compileProgram(void) {
  Object* program;

  eat(KW_PROGRAM);
  eat(TK_IDENT);

  program = createProgramObject(currentToken->string);
  program->progAttrs->codeAddress = getCurrentCodeAddress();
  enterBlock(program->progAttrs->scope);

  eat(SB_SEMICOLON);

  compileBlock();
  eat(SB_PERIOD);

  genHL();

  exitBlock();
}

void compileConstDecls(void) {
  Object* constObj;
  ConstantValue* constValue;

  if (lookAhead->tokenType == KW_CONST) {
    eat(KW_CONST);
    do {
      eat(TK_IDENT);
      checkFreshIdent(currentToken->string);
      constObj = createConstantObject(currentToken->string);
      declareObject(constObj);
      
      eat(SB_EQ);
      constValue = compileConstant();
      constObj->constAttrs->value = constValue;
      
      eat(SB_SEMICOLON);
    } while (lookAhead->tokenType == TK_IDENT);
  }
}

void compileTypeDecls(void) {
  Object* typeObj;
  Type* actualType;

  if (lookAhead->tokenType == KW_TYPE) {
    eat(KW_TYPE);
    do {
      eat(TK_IDENT);
      
      checkFreshIdent(currentToken->string);
      typeObj = createTypeObject(currentToken->string);
      declareObject(typeObj);
      
      eat(SB_EQ);
      actualType = compileType();
      typeObj->typeAttrs->actualType = actualType;
      
      eat(SB_SEMICOLON);
    } while (lookAhead->tokenType == TK_IDENT);
  } 
}

void compileVarDecls(void) {
  Object* varObj[20];
  Type* varType;
  int varCount=0;

  if (lookAhead->tokenType == KW_VAR) {
    eat(KW_VAR);
    do {
       do {
        if(lookAhead->tokenType == SB_COMMA && varCount > 0) {
          eat(SB_COMMA);
        } else if(lookAhead->tokenType == SB_COMMA && varCount == 0) {
          error(ERR_INVALID_SYMBOL, lookAhead->lineNo, lookAhead->colNo);
        } 
        eat(TK_IDENT);
        checkFreshIdent(currentToken->string);
        varObj[varCount] = createVariableObject(currentToken->string);
        varCount++;
      } while(lookAhead->tokenType == SB_COMMA);

      eat(SB_COLON);
      varType = compileType();
      for(int i = 0; i < varCount; i++) {
        varObj[i]->varAttrs->type = duplicateType(varType);
        declareObject(varObj[i]);
      }      
      eat(SB_SEMICOLON);
      varCount = 0;
    } while (lookAhead->tokenType == TK_IDENT);
  } 
}

void compileBlock(void) {
  Instruction* jmp;
  
  jmp = genJ(DC_VALUE);

  compileConstDecls();
  compileTypeDecls();
  compileVarDecls();
  compileSubDecls();

  updateJ(jmp,getCurrentCodeAddress());
  genINT(symtab->currentScope->frameSize);

  eat(KW_BEGIN);
  compileStatements();
  eat(KW_END);
}

void compileSubDecls(void) {
  while ((lookAhead->tokenType == KW_FUNCTION) || (lookAhead->tokenType == KW_PROCEDURE)) {
    if (lookAhead->tokenType == KW_FUNCTION)
      compileFuncDecl();
    else compileProcDecl();
  }
}

void compileFuncDecl(void) {
  Object* funcObj;
  Type* returnType;

  eat(KW_FUNCTION);
  eat(TK_IDENT);

  checkFreshIdent(currentToken->string);
  funcObj = createFunctionObject(currentToken->string);
  funcObj->funcAttrs->codeAddress = getCurrentCodeAddress();
  declareObject(funcObj);

  enterBlock(funcObj->funcAttrs->scope);
  
  compileParams();

  eat(SB_COLON);
  returnType = compileBasicType();
  funcObj->funcAttrs->returnType = returnType;

  eat(SB_SEMICOLON);

  compileBlock();

  genEF();
  eat(SB_SEMICOLON);

  exitBlock();
}

void compileProcDecl(void) {
  Object* procObj;

  eat(KW_PROCEDURE);
  eat(TK_IDENT);

  checkFreshIdent(currentToken->string);
  procObj = createProcedureObject(currentToken->string);
  procObj->procAttrs->codeAddress = getCurrentCodeAddress();
  declareObject(procObj);

  enterBlock(procObj->procAttrs->scope);

  compileParams();

  eat(SB_SEMICOLON);
  compileBlock();

  genEP();
  eat(SB_SEMICOLON);

  exitBlock();
}

ConstantValue* compileUnsignedConstant(void) {
  ConstantValue* constValue;
  Object* obj;

  switch (lookAhead->tokenType) {
  case TK_NUMBER:
    eat(TK_NUMBER);
    constValue = makeIntConstant(currentToken->value);
    break;
  case TK_IDENT:
    eat(TK_IDENT);

    obj = checkDeclaredConstant(currentToken->string);
    constValue = duplicateConstantValue(obj->constAttrs->value);

    break;
  case TK_CHAR:
    eat(TK_CHAR);
    constValue = makeCharConstant(currentToken->string[0]);
    break;
  default:
    error(ERR_INVALID_CONSTANT, lookAhead->lineNo, lookAhead->colNo);
    break;
  }
  return constValue;
}

ConstantValue* compileConstant(void) {
  ConstantValue* constValue;

  switch (lookAhead->tokenType) {
  case SB_PLUS:
    eat(SB_PLUS);
    constValue = compileConstant2();
    break;
  case SB_MINUS:
    eat(SB_MINUS);
    constValue = compileConstant2();
    constValue->intValue = - constValue->intValue;
    break;
  case TK_CHAR:
    eat(TK_CHAR);
    constValue = makeCharConstant(currentToken->string[0]);
    break;
  default:
    constValue = compileConstant2();
    break;
  }
  return constValue;
}

ConstantValue* compileConstant2(void) {
  ConstantValue* constValue;
  Object* obj;

  switch (lookAhead->tokenType) {
  case TK_NUMBER:
    eat(TK_NUMBER);
    constValue = makeIntConstant(currentToken->value);
    break;
  case TK_IDENT:
    eat(TK_IDENT);
    obj = checkDeclaredConstant(currentToken->string);
    if (obj->constAttrs->value->type == TP_INT)
      constValue = duplicateConstantValue(obj->constAttrs->value);
    else
      error(ERR_UNDECLARED_INT_CONSTANT,currentToken->lineNo, currentToken->colNo);
    break;
  default:
    error(ERR_INVALID_CONSTANT, lookAhead->lineNo, lookAhead->colNo);
    break;
  }
  return constValue;
}

Type* compileType(void) {
  Type* type;
  Type* elementType;
  int arraySize;
  Object* obj;

  switch (lookAhead->tokenType) {
  case KW_INTEGER: 
    eat(KW_INTEGER);
    type =  makeIntType();
    break;
  case KW_CHAR: 
    eat(KW_CHAR); 
    type = makeCharType();
    break;
  case KW_ARRAY:
    eat(KW_ARRAY);
    eat(SB_LSEL);
    eat(TK_NUMBER);

    arraySize = currentToken->value;

    eat(SB_RSEL);
    eat(KW_OF);
    elementType = compileType();
    type = makeArrayType(arraySize, elementType);
    break;
  case TK_IDENT:
    eat(TK_IDENT);
    obj = checkDeclaredType(currentToken->string);
    type = duplicateType(obj->typeAttrs->actualType);
    break;
  default:
    error(ERR_INVALID_TYPE, lookAhead->lineNo, lookAhead->colNo);
    break;
  }
  return type;
}

Type* compileBasicType(void) {
  Type* type;

  switch (lookAhead->tokenType) {
  case KW_INTEGER: 
    eat(KW_INTEGER); 
    type = makeIntType();
    break;
  case KW_CHAR: 
    eat(KW_CHAR); 
    type = makeCharType();
    break;
  default:
    error(ERR_INVALID_BASICTYPE, lookAhead->lineNo, lookAhead->colNo);
    break;
  }
  return type;
}

void compileParams(void) {
  if (lookAhead->tokenType == SB_LPAR) {
    eat(SB_LPAR);
    compileParam();
    while (lookAhead->tokenType == SB_SEMICOLON) {
      eat(SB_SEMICOLON);
      compileParam();
    }
    eat(SB_RPAR);
  }
}

void compileParam(void) {
  Object* param;
  Type* type;
  enum ParamKind paramKind = PARAM_VALUE;

  if (lookAhead->tokenType == KW_VAR) {
    paramKind = PARAM_REFERENCE;
    eat(KW_VAR);
  }

  eat(TK_IDENT);
  checkFreshIdent(currentToken->string);
  param = createParameterObject(currentToken->string, paramKind);
  eat(SB_COLON);
  type = compileBasicType();
  param->paramAttrs->type = type;
  declareObject(param);
}

void compileStatements(void) {
  compileStatement();
  while (lookAhead->tokenType == SB_SEMICOLON) {
    eat(SB_SEMICOLON);
    compileStatement();
  }
}

void compileStatement(void) {
  switch (lookAhead->tokenType) {
  case TK_IDENT:
    compileAssignSt();
    break;
  case KW_CALL:
    compileCallSt();
    break;
  case KW_BEGIN:
    compileGroupSt();
    break;
  case KW_IF:
    compileIfSt();
    break;
  case KW_WHILE:
    compileWhileSt();
    break;
  case KW_FOR:
    compileForSt();
    break;
  case KW_SWITCH:
    compileSwitchSt();
  case KW_BREAK:
    compileBreakSt();
    // EmptySt needs to check FOLLOW tokens
  case SB_SEMICOLON:
  case KW_END:
  case KW_ELSE:
  case KW_CASE:
  case KW_DEFAULT:
    break;
    // Error occurs
  default:
    error(ERR_INVALID_STATEMENT, lookAhead->lineNo, lookAhead->colNo);
    break;
  }
}

Type* compileLValue(void) {
  Object* var;
  Type* varType;

  eat(TK_IDENT);
  
  var = checkDeclaredLValueIdent(currentToken->string);

  switch (var->kind) {
  case OBJ_VARIABLE:
    genVariableAddress(var);

    if (var->varAttrs->type->typeClass == TP_ARRAY) {
      varType = compileIndexes(var->varAttrs->type);
    }
    else
      varType = var->varAttrs->type;
    break;
  case OBJ_PARAMETER:
    if (var->paramAttrs->kind == PARAM_VALUE)
      genParameterAddress(var);
    else genParameterValue(var);

    varType = var->paramAttrs->type;
    break;
  case OBJ_FUNCTION:
    genReturnValueAddress(var);
    varType = var->funcAttrs->returnType;
    break;
  default: 
    error(ERR_INVALID_LVALUE,currentToken->lineNo, currentToken->colNo);
  }

  return varType;
}

Type* compileMultLValue(Object **resVar) {
  Object* var;
  Type* varType;

  eat(TK_IDENT);
  
  var = checkDeclaredLValueIdent(currentToken->string);

  switch (var->kind) {
  case OBJ_VARIABLE:
    genVariableAddress(var);

    if (var->varAttrs->type->typeClass == TP_ARRAY) {
      varType = compileIndexes(var->varAttrs->type);
    }
    else
      varType = var->varAttrs->type;
    break;
  case OBJ_PARAMETER:
    if (var->paramAttrs->kind == PARAM_VALUE)
      genParameterAddress(var);
    else genParameterValue(var);

    varType = var->paramAttrs->type;
    break;
  case OBJ_FUNCTION:
    genReturnValueAddress(var);
    varType = var->funcAttrs->returnType;
    break;
  default: 
    error(ERR_INVALID_LVALUE,currentToken->lineNo, currentToken->colNo);
  }
  *resVar = var;
  return varType;
}

void compileAssignSt(void) {
  Type *varType;
  Type *expType;
  /*Type *conExpType;
  Type *returnType1, *returnType2;
  TokenType op;
  Instruction *fjInstruction, *jInstruction;

  varType = compileLValue();
  
  eat(SB_ASSIGN);
  if(lookAhead->tokenType==KW_IF){
      eat(KW_IF);
      compileCondition();
      if(lookAhead->tokenType==KW_THEN){
        eat(KW_THEN);
      } else if(lookAhead->tokenType==KW_RETURN){
        eat(KW_RETURN);
      }
      fjInstruction = genFJ(DC_VALUE);
      returnType1 = compileExpression();
      eat(KW_ELSE);
      if(lookAhead->tokenType==KW_RETURN){
        eat(KW_RETURN);
      }
      jInstruction = genJ(DC_VALUE);
      updateFJ(fjInstruction, getCurrentCodeAddress());
      returnType2 = compileExpression();
      checkTypeEquality(returnType1, varType);
      checkTypeEquality(returnType2, varType);
      updateJ(jInstruction, getCurrentCodeAddress());
      genST();
  } else {
      expType = compileExpression();
      op = lookAhead->tokenType;
      if(op == SB_EQ || op == SB_NEQ || op == SB_LE || op == SB_LT || op == SB_GE || op == SB_GT) {
        switch (op){
          case SB_EQ:
            eat(SB_EQ);
            break;
          case SB_NEQ:
            eat(SB_NEQ);
            break;
          case SB_LE:
            eat(SB_LE);
            break;
          case SB_LT:
            eat(SB_LT);
            break;
          case SB_GE:
            eat(SB_GE);
            break;
          case SB_GT:
            eat(SB_GT);
            break;
          default:
            break;
        }
    
        conExpType = compileExpression();
        checkTypeEquality(expType, conExpType);

        switch (op){
          case SB_EQ:
            genEQ();
            break;
          case SB_NEQ:
            genNE();
            break;
          case SB_LE:
            genLE();
            break;
          case SB_LT:
            genLT();
            break;
          case SB_GE:
            genGE();
            break;
          case SB_GT:
            genGT();
            break;
          default:
            break;
        }
        fjInstruction = genFJ(DC_VALUE);
        eat(SB_QUESTION);
        returnType1 = compileExpression();
        eat(SB_COLON);
        jInstruction = genJ(DC_VALUE);
        updateFJ(fjInstruction, getCurrentCodeAddress());
        returnType2 = compileExpression();
        checkTypeEquality(returnType1, returnType2);
        checkTypeEquality(returnType1, varType);
        updateJ(jInstruction, getCurrentCodeAddress());
      } else {
        checkTypeEquality(varType, expType);
      }
      genST();
  }  */
  int lVal = 0, rVal = 0;
  int p[1000], q[1000];
  Instruction *pc;
  Type *lType[1000], *rType[1000];

  varType = compileLValue();
  lType[lVal] = varType;
  lVal++;
  while(lookAhead->tokenType == SB_COMMA){
  	eat(SB_COMMA);
  	varType = compileLValue();
  	lType[lVal] = varType;
  	lVal++;
  }
  
  eat(SB_ASSIGN);
  
  expType = compileExpression();
  rType[rVal] = expType;
  rVal++;
  while(lookAhead->tokenType == SB_COMMA){
  	eat(SB_COMMA);
  	expType = compileExpression();
  	rType[rVal] = expType;
  	rVal++;
  }
  
  if(lVal != rVal){
  	error(ERR_LVALUE_EXPRESSION_INCONSISTENCY, currentToken->lineNo, currentToken->colNo);
  }
  for(int i = 0; i < lVal; i++){
  	checkTypeEquality(lType[i], rType[i]);
  }
  
  pc = codeBlock->code + codeBlock->codeSize - 1;
  while(pc->op != OP_LA){
  	pc--;
  }
  pc = pc - lVal + 1;
  for(int i = 0; i < lVal; i++){
  	p[i] = pc->p;
  	q[i] = pc->q;
  	pc++;
  	
  }
  genDCT(rVal+1);
  
  for(int i = 0; i < lVal; i++){
  	genLA(p[i], q[i]);
  	genINT(1);
  	genST();
  	genINT(1);
  }

}

void compileCallSt(void) {
  Object* proc;

  eat(KW_CALL);
  eat(TK_IDENT);

  proc = checkDeclaredProcedure(currentToken->string);

  
  if (isPredefinedProcedure(proc)) {
    compileArguments(proc->procAttrs->paramList);
    genPredefinedProcedureCall(proc);
  } else {
    genINT(RESERVED_WORDS);
    compileArguments(proc->procAttrs->paramList);
    genDCT( RESERVED_WORDS + proc->procAttrs->paramCount);
    genProcedureCall(proc);
  }
}

void compileGroupSt(void) {
  eat(KW_BEGIN);
  compileStatements();
  eat(KW_END);
}

void compileIfSt(void) {
  Instruction* fjInstruction;
  Instruction* jInstruction;

  eat(KW_IF);
  compileCondition();
  eat(KW_THEN);

  fjInstruction = genFJ(DC_VALUE);
  compileStatement();
  if (lookAhead->tokenType == KW_ELSE) {
    jInstruction = genJ(DC_VALUE);
    updateFJ(fjInstruction, getCurrentCodeAddress());
    eat(KW_ELSE);
    compileStatement();
    updateJ(jInstruction, getCurrentCodeAddress());
  } else {
    updateFJ(fjInstruction, getCurrentCodeAddress());
  }
}

void compileWhileSt(void) {
  CodeAddress beginWhile;
  Instruction* fjInstruction;

  beginWhile = getCurrentCodeAddress();
  eat(KW_WHILE);
  compileCondition();
  fjInstruction = genFJ(DC_VALUE);
  eat(KW_DO);
  compileStatement();
  genJ(beginWhile);
  updateFJ(fjInstruction, getCurrentCodeAddress());
}

void compileForSt(void) {
  CodeAddress beginLoop;
  Instruction* fjInstruction;
  Type* varType;
  Type *type;

  eat(KW_FOR);

  varType = compileLValue();
  eat(SB_ASSIGN);

  genCV();
  type = compileExpression();
  checkTypeEquality(varType, type);
  genST();
  genCV();
  genLI();
  beginLoop = getCurrentCodeAddress();
  eat(KW_TO);

  type = compileExpression();
  checkTypeEquality(varType, type);
  genLE();
  fjInstruction = genFJ(DC_VALUE);

  eat(KW_DO);
  compileStatement();

  genCV();  
  genCV();
  genLI();
  genLC(1);
  genAD();
  genST();

  genCV();
  genLI();

  genJ(beginLoop);
  updateFJ(fjInstruction, getCurrentCodeAddress());
  genDCT(1);

}

void compileCaseSt(Type *argType)
{
  eat(KW_CASE);
  genCV();
  ConstantValue *constantValue = compileConstant();
  Type type2;
  type2.typeClass = constantValue->type;
  checkTypeEquality(argType, &type2);
  eat(SB_COLON);
  if (constantValue->type == TP_INT)
  {
    genLC(constantValue->intValue);
  }
  else if (constantValue->type == TP_CHAR)
  {
    genLC(constantValue->charValue);
  }

  genEQ();
  Instruction *fjInstruction = genFJ(DC_VALUE);
  compileStatements();
  updateFJ(fjInstruction, getCurrentCodeAddress());
  
  if (lookAhead->tokenType == KW_CASE)
  {
    compileCaseSt(argType);
  }
}

void compileDefaultSt(void)
{
  eat(KW_DEFAULT);
  eat(SB_COLON);
  compileStatements();
}

void compileBreakSt(void)
{
  if (lookAhead->tokenType == KW_BREAK)
  {
    eat(KW_BREAK);
    breakInstruction[breakInstructionCount++] = genJ(DC_VALUE);
  }
}

void compileSwitchSt(void)
{
  Type *type1;
  eat(KW_SWITCH);
  type1 = compileExpression();
  checkBasicType(type1);
  eat(KW_BEGIN);
  compileCaseSt(type1);
  compileDefaultSt();
  eat(KW_END);
  for (int i = 0; i < breakInstructionCount; i++)
  {
    updateJ(breakInstruction[i], getCurrentCodeAddress());
  }
}

void compileArgument(Object* param) {
  Type* type;

  if (param->paramAttrs->kind == PARAM_VALUE) {
    type = compileExpression();
    checkTypeEquality(type, param->paramAttrs->type);
  } else {
    type = compileLValue();
    checkTypeEquality(type, param->paramAttrs->type);
  }
}

void compileArguments(ObjectNode* paramList) {
  ObjectNode* node = paramList;

  switch (lookAhead->tokenType) {
  case SB_LPAR:
    eat(SB_LPAR);
    if (node == NULL)
      error(ERR_PARAMETERS_ARGUMENTS_INCONSISTENCY, currentToken->lineNo, currentToken->colNo);
    compileArgument(node->object);
    node = node->next;

    while (lookAhead->tokenType == SB_COMMA) {
      eat(SB_COMMA);
      if (node == NULL)
	error(ERR_PARAMETERS_ARGUMENTS_INCONSISTENCY, currentToken->lineNo, currentToken->colNo);
      compileArgument(node->object);
      node = node->next;
    }

    if (node != NULL)
      error(ERR_PARAMETERS_ARGUMENTS_INCONSISTENCY, currentToken->lineNo, currentToken->colNo);
    
    eat(SB_RPAR);
    break;
    // Check FOLLOW set 
  case SB_TIMES:
  case SB_SLASH:
  case SB_PLUS:
  case SB_MINUS:
  case KW_TO:
  case KW_DO:
  case SB_RPAR:
  case SB_COMMA:
  case SB_EQ:
  case SB_NEQ:
  case SB_LE:
  case SB_LT:
  case SB_GE:
  case SB_GT:
  case SB_RSEL:
  case SB_SEMICOLON:
  case KW_END:
  case KW_ELSE:
  case KW_THEN:
    break;
  default:
    error(ERR_INVALID_ARGUMENTS, lookAhead->lineNo, lookAhead->colNo);
  }
}

void compileCondition(void) {
  Type* type1;
  Type* type2;
  TokenType op;

  type1 = compileExpression();
  checkBasicType(type1);

  op = lookAhead->tokenType;
  switch (op) {
  case SB_EQ:
    eat(SB_EQ);
    break;
  case SB_NEQ:
    eat(SB_NEQ);
    break;
  case SB_LE:
    eat(SB_LE);
    break;
  case SB_LT:
    eat(SB_LT);
    break;
  case SB_GE:
    eat(SB_GE);
    break;
  case SB_GT:
    eat(SB_GT);
    break;
  default:
    error(ERR_INVALID_COMPARATOR, lookAhead->lineNo, lookAhead->colNo);
  }

  type2 = compileExpression();
  checkTypeEquality(type1,type2);

  switch (op) {
  case SB_EQ:
    genEQ();
    break;
  case SB_NEQ:
    genNE();
    break;
  case SB_LE:
    genLE();
    break;
  case SB_LT:
    genLT();
    break;
  case SB_GE:
    genGE();
    break;
  case SB_GT:
    genGT();
    break;
  default:
    break;
  }

}

Type* compileExpression(void) {
  Type* type;
  
  switch (lookAhead->tokenType) {
  case SB_PLUS:
    eat(SB_PLUS);
    type = compileExpression2();
    checkIntType(type);
    break;
  case SB_MINUS:
    eat(SB_MINUS);
    type = compileExpression2();
    checkIntType(type);
    genNEG();
    break;
  default:
    type = compileExpression2();
  }
  return type;
}

Type* compileExpression2(void) {
  Type* type;

  type = compileTerm();
  type = compileExpression3(type);

  return type;
}


Type* compileExpression3(Type* argType1) {
  Type* argType2;
  Type* resultType;

  switch (lookAhead->tokenType) {
  case SB_PLUS:
    eat(SB_PLUS);
    checkIntType(argType1);
    argType2 = compileTerm();
    checkIntType(argType2);

    genAD();

    resultType = compileExpression3(argType1);
    break;
  case SB_MINUS:
    eat(SB_MINUS);
    checkIntType(argType1);
    argType2 = compileTerm();
    checkIntType(argType2);

    genSB();

    resultType = compileExpression3(argType1);
    break;
    // check the FOLLOW set
  case KW_TO:
  case KW_DO:
  case SB_RPAR:
  case SB_COMMA:
  case SB_COLON:
  case SB_EQ:
  case SB_NEQ:
  case SB_LE:
  case SB_LT:
  case SB_GE:
  case SB_GT:
  case SB_QUESTION:
  case SB_RSEL:
  case SB_SEMICOLON:
  case KW_BEGIN:
  case KW_END:
  case KW_ELSE:
  case KW_THEN:
  case KW_RETURN:
    resultType = argType1;
    break;
  default:
    error(ERR_INVALID_EXPRESSION, lookAhead->lineNo, lookAhead->colNo);
  }
  return resultType;
}

Type* compileTerm(void) {
  Type* type;
  type = compileFactor0();
  type = compileTerm2(type);
  return type;
}
Type* compileTerm2(Type* argType1) {
  Type* argType2;
  Type* resultType;

  switch (lookAhead->tokenType) {
  case SB_TIMES:
    eat(SB_TIMES);
    checkIntType(argType1);
    argType2 = compileFactor0();
    checkIntType(argType2);

    genML();

    resultType = compileTerm2(argType1);
    break;
  case SB_SLASH:
    eat(SB_SLASH);
    checkIntType(argType1);
    argType2 = compileFactor0();
    checkIntType(argType2);

    genDV();

    resultType = compileTerm2(argType1);
    break;
  case SB_MODULE:
    eat(SB_MODULE);
    checkIntType(argType1);
    genCV();
    argType2 = compileFactor0();
    checkIntType(argType2);
    genDV();
    genINT(1);
    genML();
    genSB();
    resultType = compileTerm2(argType1);
    break;
    // check the FOLLOW set
  case SB_PLUS:
  case SB_MINUS:
  case KW_TO:
  case KW_DO:
  case SB_RPAR:
  case SB_COMMA:
  case SB_COLON:
  case SB_EQ:
  case SB_NEQ:
  case SB_LE:
  case SB_LT:
  case SB_GE:
  case SB_GT:
  case SB_QUESTION:
  case SB_RSEL:
  case SB_SEMICOLON:
  case KW_BEGIN:
  case KW_END:
  case KW_ELSE:
  case KW_THEN:
  case KW_RETURN:
    resultType = argType1;
    break;
  default:
    error(ERR_INVALID_TERM, lookAhead->lineNo, lookAhead->colNo);
  }
  return resultType;
}
Type* compileFactor0(void) {
  Type* type;
  type = compileFactor();
  type = compileTerm3(type);
  return type;
}

Type* compileTerm3(Type* argType1) {
  Type* argType2;
  Type* resultType;
  CodeAddress beginLoop;
  Instruction *fjInst;

  switch (lookAhead->tokenType) {
  case SB_POWER:
    eat(SB_POWER);
  	argType2 = compileFactor();
      checkIntType(argType2);
      genCV();
      genDCT(2);
      genCV();
      genDCT(2);
      genLC(1);
      genINT(2);
      beginLoop = getCurrentCodeAddress();
      genCV();
      genLC(0);
      genGT();
      fjInst = genFJ(DC_VALUE);
      genDCT(1);
      genML();
      genINT(2);
      genLC(1);
      genSB();
      genJ(beginLoop);
      updateFJ(fjInst, getCurrentCodeAddress());
      genDCT(2);
  	resultType = compileTerm2(argType1);
  	break;
    // check the FOLLOW set
  case SB_PLUS:
  case SB_MINUS:
  case SB_TIMES:
  case SB_SLASH:
  case SB_MODULE:
  case KW_TO:
  case KW_DO:
  case SB_RPAR:
  case SB_COMMA:
  case SB_COLON:
  case SB_EQ:
  case SB_NEQ:
  case SB_LE:
  case SB_LT:
  case SB_GE:
  case SB_GT:
  case SB_QUESTION:
  case SB_RSEL:
  case SB_SEMICOLON:
  case KW_BEGIN:
  case KW_END:
  case KW_ELSE:
  case KW_THEN:
  case KW_RETURN:
    resultType = argType1;
    break;
  default:
    error(ERR_INVALID_TERM, lookAhead->lineNo, lookAhead->colNo);
  }
  return resultType;
}

Type* compileFactor(void) {
  Type* type;
  Object* obj;

  switch (lookAhead->tokenType) {
  case TK_NUMBER:
    eat(TK_NUMBER);
    type = intType;
    genLC(currentToken->value);
    break;
  case TK_CHAR:
    eat(TK_CHAR);
    type = charType;
    genLC(currentToken->value);
    break;
  case TK_IDENT:
    eat(TK_IDENT);
    obj = checkDeclaredIdent(currentToken->string);

    switch (obj->kind) {
    case OBJ_CONSTANT:
      switch (obj->constAttrs->value->type) {
      case TP_INT:
	      type = intType;
	      genLC(obj->constAttrs->value->intValue);
	      break;
      case TP_CHAR:
	      type = charType;
	      genLC(obj->constAttrs->value->charValue);
	      break;
      default:
	      break;
      }
      break;
    case OBJ_VARIABLE:
      if (obj->varAttrs->type->typeClass == TP_ARRAY) {
	      genVariableAddress(obj);
	      type = compileIndexes(obj->varAttrs->type);
	      genLI();
      } else {
	      type = obj->varAttrs->type;
	      genVariableValue(obj);
      }
      break;
    case OBJ_PARAMETER:
      type = obj->paramAttrs->type;
      genParameterValue(obj);
      if (obj->paramAttrs->kind == PARAM_REFERENCE)
	      genLI();
      break;
    case OBJ_FUNCTION:
      if (isPredefinedFunction(obj)) {
	      compileArguments(obj->funcAttrs->paramList);
	      genPredefinedFunctionCall(obj);
      } else {
	      genINT(4);
	      compileArguments(obj->funcAttrs->paramList);
	      genDCT(4+obj->funcAttrs->paramCount);
	      genFunctionCall(obj);
      }
      type = obj->funcAttrs->returnType;
      break;
    default: 
      error(ERR_INVALID_FACTOR,currentToken->lineNo, currentToken->colNo);
      break;
    }
    break;
  case SB_LPAR:
    eat(SB_LPAR);
    type = compileExpression();
    eat(SB_RPAR);
    break;
  default:
    error(ERR_INVALID_FACTOR, lookAhead->lineNo, lookAhead->colNo);
  }
  
  return type;
}

Type* compileIndexes(Type* arrayType) {
  Type* type;

  
  while (lookAhead->tokenType == SB_LSEL) {
    eat(SB_LSEL);
    type = compileExpression();
    checkIntType(type);
    checkArrayType(arrayType);

    genLC(sizeOfType(arrayType->elementType));
    genML();
    genAD();

    arrayType = arrayType->elementType;
    eat(SB_RSEL);
  }
  checkBasicType(arrayType);
  return arrayType;
}

int compile(char *fileName) {
  if (openInputStream(fileName) == IO_ERROR)
    return IO_ERROR;

  currentToken = NULL;
  lookAhead = getValidToken();

  initSymTab();

  compileProgram();

  cleanSymTab();
  free(currentToken);
  free(lookAhead);
  closeInputStream();
  return IO_SUCCESS;

}
