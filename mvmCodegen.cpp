/* 
 * File:   mvmCodegen.cpp
 * Author: ghernan
 * 
 * Code generator for micro VM,
 * 
 * Created on December 2, 2016, 9:59 PM
 */

#include "ascript_pch.hpp"
#include "mvmCodegen.h"
#include "asObjects.h"
#include "ScriptException.h"

#include <set>
#include <map>
#include <string>

using namespace std;

/**
 * Code generation scope
 */
class CodegenScope 
{
public:
    void declare (const std::string& name, int stackPos)
    {
        m_symbols[name] = stackPos;
    }

    bool isDeclared (const std::string& name)const
    {
        return m_symbols.count(name) > 0;
    }
    
    int symbolPosition (const string& name)const
    {
        ASSERT (isDeclared(name));
        return m_symbols.at(name);
    }
    
    const Ref<AstNode>          ownerNode;
    const bool                  isBlock;
    const bool                  isParameters;
    
    CodegenScope (const Ref<AstNode> _ownerNode, bool block, bool params) 
    : ownerNode (_ownerNode), isBlock(block), isParameters(params)
    {
    }
    
private:
    std::map < std::string, int>    m_symbols;
};

typedef map<ASValue, int> ConstantsMap;

/**
 * State of a codegen operation
 */
struct CodegenState
{
    Ref<MvmRoutine>             curRoutine;
    VarMap                      members;
    ConstantsMap                constants;
    map<string, ASValue >  symbols;
    ScriptPosition              curPos;
    CodeMap*                    pCodeMap = NULL;
    int                         stackSize = 0;
    
    void declare (const std::string& name)
    {
        assert (!m_scopes.empty());
        m_scopes.back().declare(name, stackSize);
    }

    bool isDeclared (const std::string& name)const
    {
        auto it = m_scopes.rbegin();
        
        for (; it != m_scopes.rend(); ++it)
        {
            if (it->isDeclared(name))
                return true;
            else if (!it->isBlock)
                return false;
        }
        
        return false;
    }
    
    bool isParam (const std::string& name)const
    {
        auto it = m_scopes.rbegin();
        
        for (; it != m_scopes.rend(); ++it)
        {
            if (it->isDeclared(name))
                return it->isParameters;
        }
        
        return false;
    }
    
    int getLocalVarOffset(const string& name)const
    {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it)
        {
            if (it->isDeclared(name))
            {
                int pos = it->symbolPosition(name);
                ASSERT (pos < stackSize);
                return stackSize - (pos+1);
            }
        }
        
        ASSERT (!"Local symbol not found!");
        return -1;
    }
    
    int getParamIndex(const string& name)const
    {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it)
        {
            if (it->isDeclared(name))
            {
                if (!it->isParameters)
                    break;
                
                return it->symbolPosition(name);
            }
        }
        
        ASSERT (!"Parameter not found!");
        return -1;
    }
    
    void pushScope (const Ref<AstNode> _ownerNode, bool block, bool params)
    {
        m_scopes.push_back(CodegenScope(_ownerNode, block, params));
    }
   
    void popScope()
    {
        m_scopes.pop_back();
    }
    
    CodegenScope* curScope ()
    {
        return &m_scopes.back();
    }
    
private:
    vector<CodegenScope>        m_scopes;
};

//Forward declarations

typedef void (*NodeCodegenFN)(Ref<AstNode> node, CodegenState* pState);

void codegen (Ref<AstNode> statement, CodegenState* pState);
int  childrenCodegen (Ref<AstNode> statement, CodegenState* pState);
bool childCodegen (Ref<AstNode> statement, int index, CodegenState* pState);

void invalidNodeCodegen (Ref<AstNode> node, CodegenState* pState);
void blockCodegen (Ref<AstNode> statement, CodegenState* pState);
void varCodegen (Ref<AstNode> statement, CodegenState* pState);
void varCodegen (const string& name, Ref<AstNode> valueNode, bool isConst, CodegenState* pState);
void ifCodegen (Ref<AstNode> statement, CodegenState* pState);
void forCodegen (Ref<AstNode> statement, CodegenState* pState);
void forEachCodegen (Ref<AstNode> statement, CodegenState* pState);
void returnCodegen (Ref<AstNode> statement, CodegenState* pState);
void functionCodegen (Ref<AstNode> statement, CodegenState* pState);
void closureCodegen (Ref<JSFunction> fn, CodegenState* pState);
Ref<JSFunction> createFunction (Ref<AstNode> node, CodegenState* pState);
void assignmentCodegen (Ref<AstNode> statement, CodegenState* pState);
void varWriteCodegen (Ref<AstNode> node, CodegenState* pState);
void fieldWriteCodegen (Ref<AstNode> node, CodegenState* pState);
void arrayWriteCodegen (Ref<AstNode> node, CodegenState* pState);
void fncallCodegen (Ref<AstNode> statement, CodegenState* pState);
void thisCallCodegen (Ref<AstNode> statement, CodegenState* pState);
void literalCodegen (Ref<AstNode> statement, CodegenState* pState);
void varReadCodegen (Ref<AstNode> node, CodegenState* pState);
void varReadCodegen (const string& name, CodegenState* pState);
void arrayCodegen (Ref<AstNode> statement, CodegenState* pState);
void objectCodegen (Ref<AstNode> statement, CodegenState* pState);
void arrayAccessCodegen (Ref<AstNode> statement, CodegenState* pState);
void memberAccessCodegen (Ref<AstNode> statement, CodegenState* pState);
void conditionalCodegen (Ref<AstNode> statement, CodegenState* pState);
void binaryOpCodegen (Ref<AstNode> statement, CodegenState* pState);
void prefixOpCodegen (Ref<AstNode> statement, CodegenState* pState);
void postfixOpCodegen (Ref<AstNode> statement, CodegenState* pState);
void logicalOpCodegen (const int opCode, Ref<AstNode> statement, CodegenState* pState);

void actorCodegen (Ref<AstNode> node, CodegenState* pState);
void connectCodegen (Ref<AstNode> node, CodegenState* pState);
void messageCodegen (Ref<AstNode> node, CodegenState* pState);

void clearLocals (int targetStackSize, CodegenState* pState);

void classCodegen (Ref<AstNode> node, CodegenState* pState);
Ref<JSFunction> classConstructorCodegen (Ref<AstNode> node, CodegenState* pState);
void baseConstructorCallCodegen (Ref<AstNode> node, CodegenState* pState);
StringVector classConstructorParams(Ref<AstNode> node, CodegenState* pState);
Ref<JSClass> getParentClass (Ref<AstNode> node, CodegenState* pState);

void exportCodegen (Ref<AstNode> node, CodegenState* pState);
void importCodegen (Ref<AstNode> node, CodegenState* pState);

void pushConstant (ASValue value, CodegenState* pState);
void pushConstant (const char* str, CodegenState* pState);
void pushConstant (const std::string& str, CodegenState* pState);
void pushConstant (int value, CodegenState* pState);
void pushConstant (bool value, CodegenState* pState);
void pushNull (CodegenState* pState);

void callCodegen (const std::string& fnName, int nParams, CodegenState* pState, const ScriptPosition& pos);
void callInstruction (int nParams, CodegenState* pState, const ScriptPosition& pos);
void copyInstruction (int offset, CodegenState* pState);
void writeInstruction (int offset, CodegenState* pState);

void instruction8 (int opCode, CodegenState* pState);
void instruction16 (int opCode, CodegenState* pState);
int  getLastInstruction (CodegenState* pState);
int  removeLastInstruction (CodegenState* pState);
void binaryOperatorCode (int tokenCode, CodegenState* pState, const ScriptPosition& pos);
void getEnvCodegen (CodegenState* pState);

void endBlock (int trueJump, int falseJump, CodegenState* pState);
void setTrueJump (int blockId, int destinationId, CodegenState* pState);
void setFalseJump (int blockId, int destinationId, CodegenState* pState);
int  curBlockId (CodegenState* pState);

bool isCopyInstruction(int opCode);

int  getAssignOp (Ref<AstNode> node);

int calcStackOffset8(int opCode);
int calcStackOffset16(int opCode);

CodegenState initFunctionState (Ref<AstNode> node, const StringVector& params, CodeMap* pMap);
CodegenState initFunctionState (Ref<AstNode> node, CodeMap* pMap);

/**
 * Generates MVM code for a script.
 * @param script    Script AST node.
 * @return 
 */
Ref<MvmRoutine> scriptCodegen (Ref<AstNode> script, CodeMap* pMap)
{
    CodegenState    state;
    
    ASSERT (script->getType() == AST_SCRIPT);
    
    state.curRoutine = MvmRoutine::create();
    state.pushScope(script, false, false);
    state.pCodeMap = pMap;
    state.curPos = script->position();
    
    auto statements = script->children();
    
    for (size_t i = 0; i < statements.size(); ++i)
    {
        //Remove previous result
        if (i > 0)
            instruction8 (OC_POP, &state);
        codegen (statements[i], &state);
    }
    
    return state.curRoutine;
}


void codegen (Ref<AstNode> statement, CodegenState* pState)
{
    static NodeCodegenFN types[AST_TYPES_COUNT] = {NULL, NULL};
    
    if (types [0] == NULL)
    {
        types [AST_SCRIPT] = invalidNodeCodegen;
        types [AST_BLOCK] = blockCodegen;
        types [AST_VAR] = varCodegen;
        types [AST_CONST] = varCodegen;
        types [AST_IF] = ifCodegen;
        types [AST_FOR] = forCodegen;
        types [AST_FOR_EACH] = forEachCodegen;
        types [AST_RETURN] = returnCodegen;
        types [AST_FUNCTION] = functionCodegen;
        types [AST_ASSIGNMENT] = assignmentCodegen;
        types [AST_FNCALL] = fncallCodegen;
        types [AST_LITERAL] = literalCodegen;
        types [AST_IDENTIFIER] = varReadCodegen;
        types [AST_ARRAY] = arrayCodegen;
        types [AST_OBJECT] = objectCodegen;
        types [AST_ARRAY_ACCESS] = arrayAccessCodegen;
        types [AST_MEMBER_ACCESS] = memberAccessCodegen;
        types [AST_CONDITIONAL] = conditionalCodegen;
        types [AST_BINARYOP] = binaryOpCodegen;
        types [AST_PREFIXOP] = prefixOpCodegen;
        types [AST_POSTFIXOP] = postfixOpCodegen;
        types [AST_ACTOR] = actorCodegen;
        types [AST_CONNECT] = connectCodegen;
        types [AST_INPUT] = messageCodegen;
        types [AST_OUTPUT] = messageCodegen;
        types [AST_CLASS] = classCodegen;
        types [AST_EXPORT] = exportCodegen;
        types [AST_IMPORT] = importCodegen;
    }
    
    auto oldPos = pState->curPos;
    pState->curPos = statement->position();
    
    types[statement->getType()](statement, pState);
    
    pState->curPos = oldPos;
}

/**
 * Generates code for the children of a given statement
 * @param statement
 * @param pState
 */
int childrenCodegen (Ref<AstNode> statement, CodegenState* pState)
{
    const AstNodeList&  children = statement->children();
    int count = 0;
    
    for (size_t i=0; i < children.size(); ++i)
    {
        if (children[i].notNull())
        {
            codegen (children[i], pState);
            ++count;
        }
    }
    
    return count;
}

/**
 * Generates code for a child node of an AST node.
 * @param statement
 * @param index
 * @param pState
 * @return 
 */
bool childCodegen (Ref<AstNode> statement, int index, CodegenState* pState)
{
    const AstNodeList&  children = statement->children();
    
    if (index >= (int)children.size() || index < 0)
        return false;
    else if (children[index].notNull())
    {
        codegen (children[index], pState);
        return true;
    }
    else
        return false;
}

/**
 * Called if an invalid node is found in the AST
 * @param node
 * @param pState
 */
void invalidNodeCodegen(Ref<AstNode> node, CodegenState* pState)
{
    auto typeString = astTypeToString(node->getType());
    errorAt (node->position(), "Invalid AST node found: ", typeString.c_str());
}

/**
 * Generates the code for a block of statements.
 * @param statement
 * @param pState
 */
void blockCodegen (Ref<AstNode> statement, CodegenState* pState)
{
    const AstNodeList&  children = statement->children();
    
    pState->pushScope(statement, true, false);
    const int stackSize = pState->stackSize;
    
    for (size_t i=0; i < children.size(); ++i)
    {
        if (children[i].notNull())
        {
            codegen (children[i], pState);
            instruction8(OC_POP, pState);   //Discard result
        }
    }

    clearLocals(stackSize, pState);

    pState->popScope();
        
    //Non expression statements leave a 'null' on the stack.
    pushNull(pState);
}

/**
 * Generates code for a 'var' declaration
 * @param statement
 * @param pState
 */
void varCodegen (Ref<AstNode> node, CodegenState* pState)
{
    const string name = node->getName();
    const bool isLocal = pState->curScope()->isBlock;
    const bool isConst = node->getType() == AST_CONST;

    if (isLocal)
    {
        pState->declare(name);

        if (!childCodegen(node, 0, pState))
            pushNull(pState);
    }
    else
    {
        getEnvCodegen(pState);              //[env]
        pushConstant(name, pState);         //[env, name]
        
        if (!childCodegen(node, 0, pState))
            pushNull(pState);
                                            //[env, name, value]
        const int writeInst = isConst ? OC_NEW_CONST_FIELD : OC_WR_FIELD;
        instruction8(writeInst, pState);  //[value]
        instruction8(OC_POP, pState);       //[]
        
        //TODO: It is a bit odd to throw away the value and replace it
        //by a 'null' on the stack.
    }
    //Non-expression statements leave a 'null' on the stack.
    pushNull(pState);                       //[null]
}

/**
 * Generates code for an 'if' statement
 * @param statement
 * @param pState
 */
void ifCodegen (Ref<AstNode> statement, CodegenState* pState)
{
    const int conditionBlock = curBlockId(pState)+1;
    const bool conditional = statement->getType() == AST_CONDITIONAL;
    
    //Generate code for condition
    pushNull(pState);
    endBlock (conditionBlock, conditionBlock, pState);
    childCodegen(statement, 0, pState);
    
    const int thenInitialBlock = curBlockId(pState)+1;
    endBlock (thenInitialBlock, -1, pState);
    
    const int postConditionStack = pState->stackSize;
    
    //Generate code for 'then' block
    childCodegen(statement, 1, pState);
    if (conditional)
        copyInstruction(0, pState);
    const int thenFinalBlock = curBlockId(pState);
    int nextBlock = thenFinalBlock + 1;
    const int elseBlock = nextBlock;
    endBlock (nextBlock, nextBlock, pState);
    
    const int postThenStack = pState->stackSize;
    
    //Try to generate 'else'
    if (statement->childExists(2))
    {
        pState->stackSize = postConditionStack;
        childCodegen(statement, 2, pState);
        if (conditional)
            copyInstruction(0, pState);
        
        //Recalculate 'next' block
        nextBlock = curBlockId(pState)+1;
        endBlock (nextBlock, nextBlock, pState);
        
        //Fix 'then' jump destination
        setTrueJump (thenFinalBlock, nextBlock, pState);
        setFalseJump (thenFinalBlock, nextBlock, pState);
    }
    
    ASSERT (pState->stackSize == postThenStack);

    //Fix else jump
    setFalseJump (thenInitialBlock-1, elseBlock, pState);
    
    //Non-expression statements leave a 'null' on the stack.
    //TODO: make if expressions?
    if (!conditional)
        pushNull(pState);
}

/**
 * Generates code for a 'for' loop.
 * It also implements 'while' loops, as they are just a for without initialization 
 * and increment statements. 
 * @param statement
 * @param pState
 */
void forCodegen (Ref<AstNode> statement, CodegenState* pState)
{
    //For loops define its own scope
    const int initialStack = pState->stackSize;
    
    //Loop initialization
    if (!childCodegen (statement, 0, pState))
        pushNull(pState);
    
    const int conditionBlock = curBlockId(pState)+1;
    //Generate code for condition
    endBlock (conditionBlock, conditionBlock, pState);
    if (!childCodegen(statement, 1, pState))
    {
        //If there is no condition, we replace it by an 'always true' condition
        pushConstant(true, pState);
    }
    const int bodyBegin = curBlockId(pState)+1;
    endBlock (bodyBegin, -1, pState);

    //loop body & increment
    if (childCodegen (statement, 3, pState))
        instruction8(OC_POP, pState);

    if (!childCodegen (statement, 2, pState))
        pushNull(pState);
    
    const int nextBlock = curBlockId(pState)+1;
    endBlock(conditionBlock, conditionBlock, pState);
    
    //Fix condition jump destination
    setFalseJump(bodyBegin-1, nextBlock, pState);    
    
    //Remove loop scope
    clearLocals(initialStack, pState);
    
    //Non-expression statements leave a 'null' on the stack.
    pushNull(pState);
}

/**
 * Generates code for a 'for (... in ...)' loop, which iterates over the members 
 * of a sequence.
 * @param node
 * @param pState
 */
void forEachCodegen (Ref<AstNode> node, CodegenState* pState)
{
    const ScriptPosition    pos = node->position();
    
    //Loop initialization
    childCodegen (node, 1, pState);             //[sequence]
    
    //Get iterator.
    callCodegen ("@iterator",1, pState, pos);  //[iterator]
        
    //Generate code for condition
    pushConstant(jsNull(), pState);                         //[null, iterator]
    const int conditionBlock = curBlockId(pState)+1;
    endBlock (conditionBlock, conditionBlock, pState);      //[iterator]
 
    instruction8(OC_CP, pState);                    //[iterator, iterator]
    pushNull(pState);                               //[null, iterator, iterator]
    callCodegen("@notTypeEqual", 2, pState, pos);   //[bool, iterator]
    endBlock (conditionBlock+1, -1, pState);        //[iterator]
    
    //Generate code for body
    pState->pushScope(node, true, false);

    //Declare item variable name, and read its content
    string itemVarName = node->children().front()->getName();
    pState->declare(itemVarName);
    copyInstruction(0, pState);                 //[iterator, iterator]
    instruction8(OC_WR_THISP, pState);
    pushConstant("head", pState);               //["head", iterator, iterator]
    instruction8(OC_RD_FIELD, pState);          //[headFn, iterator]
    callInstruction(0, pState, node->children().front()->position());   //[item, iterator]
    
    childCodegen(node, 2, pState);              //[body_result, item, iterator]
    instruction8(OC_POP, pState);               //[item, iterator]
    instruction8(OC_POP, pState);               //[iterator]
    
    pState->popScope();
    
    //Next iterator.
    instruction8(OC_WR_THISP, pState);
    pushConstant("tail", pState);               //["tail", iterator]
    instruction8(OC_RD_FIELD, pState);          //[tailFn]
    callInstruction(0, pState, pos);            //[nextIterator]
    pushConstant(jsNull(), pState);             //[null, nextIterator]
    
    endBlock(conditionBlock, conditionBlock, pState);   //[nextIterator]
    
    //Block after the loop
    const int nextBlock = curBlockId(pState);
    
    //Fix condition jump destination
    setFalseJump(conditionBlock, nextBlock, pState);    

    //[iterator] is left on the stack, which should be null at the end of the loop.
}

/**
 * Generates code for a 'return' statement
 * @param node
 * @param pState
 */
void returnCodegen (Ref<AstNode> node, CodegenState* pState)
{
    //It just pushes return expression value on the stack, and sets next block 
    //indexes to (-1), which means that the current function shall end.
    if (!childCodegen(node, 0, pState))
    {
        //If it is an empty return statement, push a 'null' value on the stack
        pushNull(pState);
    }
    
    //remove locals from the stack
    if (pState->stackSize > 1)
    {
        //Write the result at the new stack top.
        writeInstruction(pState->stackSize-2, pState);
        
        //shrink the stack
        while (pState->stackSize > 1)
            instruction8(OC_POP, pState);
    }
    
    ASSERT (pState->stackSize == 1);
    
    endBlock(-1, -1, pState);
}

/**
 * Generates code for a function declaration.
 * @param node
 * @param pState
 */
void functionCodegen (Ref<AstNode> node, CodegenState* pState)
{
    //TODO: Closure code generation. Necessary to access globals
    auto    function = createFunction(node, pState);
    
    if (node->getName().empty())
        closureCodegen(function, pState);   //Unnamed function
    else
    {
        const string name = node->getName();
        const bool isLocal = pState->curScope()->isBlock;

        if (isLocal)
        {
            pState->declare(name);

            //Functions are expressions, even named ones
            closureCodegen(function, pState);
            copyInstruction (0, pState);
            
            //[function, function] (local variable and result)
        }
        else
        {
            getEnvCodegen(pState);              //[env]
            pushConstant(name, pState);         //[env, name]
            closureCodegen(function, pState);   //[env, name, function]
                                                
            instruction8(OC_NEW_CONST_FIELD, pState);  //[function]
        }
    }
}

/**
 * Generates code for a closure
 * A closure is a function + environment.
 * @param fn
 * @param pState
 */
void closureCodegen (Ref<JSFunction> fn, CodegenState* pState)
{
    //TODO: This first version just passes the current environment.
    getEnvCodegen(pState);                                  //[env]
    pushConstant(fn->value(), pState);                      //[env, function]
    callCodegen("@makeClosure", 2, pState, pState->curPos); //[closure]
}


/**
 * Compiles and creates a function.
 * @param node
 * @param pState
 * @return 
 */
Ref<JSFunction> createFunction (Ref<AstNode> node, CodegenState* pState)
{
    Ref<AstFunction>    fnNode = node.staticCast<AstFunction>();
    const AstFunction::Params& params = fnNode->getParams();
    
    CodegenState    fnState = initFunctionState(node, pState->pCodeMap);

    auto    function = JSFunction::createJS(fnNode->getName(), params, fnState.curRoutine);

    codegen (fnNode->getCode(), &fnState);
    
    return function;
}

/**
 * Generates code for assignments
 * @param node
 * @param pState
 */
void assignmentCodegen (Ref<AstNode> node, CodegenState* pState)
{
    auto& children = node->children();
    ASSERT (children.size() == 2);
    
    auto& lvalue = children[0];
    
    switch (lvalue->getType())
    {
    case AST_IDENTIFIER:
        varWriteCodegen(node, pState);
        break;
        
    case AST_MEMBER_ACCESS:
        fieldWriteCodegen(node, pState);
        break;
        
    case AST_ARRAY_ACCESS:
        arrayWriteCodegen(node, pState);
        break;
        
    default:
        ASSERT (!"Unexpected lvalue node in assignment");
    }
}

/**
 * Generate code which writes into a variable.
 * @param node
 * @param pState
 */
void varWriteCodegen (Ref<AstNode> node, CodegenState* pState)
{
    string  name = node->children().front()->getName();
    int     op = getAssignOp(node);
    
    if (pState->isDeclared(name))
    {
        const bool isParam = pState->isParam(name);
        
        if (isParam)
            pushConstant (pState->getParamIndex(name), pState); //[index]
        
        //Local variable.
        if (op == '=')
            childCodegen(node, 1, pState);                      //[..., result]
        else
        {
            childCodegen(node, 0, pState);                      //[..., lvalue]
            childCodegen(node, 1, pState);                      //[..., lvalue, rvalue]
            binaryOperatorCode (op, pState, node->position());  //[..., result]          
        }
        
        if (pState->isParam(name))
            instruction8(OC_WR_PARAM, pState);                              //[result]
        else
            writeInstruction(pState->getLocalVarOffset(name)-1, pState);    //[result]
    }
    else
    {
        //environment-based variable (global, closure...)
        getEnvCodegen(pState);                  //[env]
        pushConstant(name, pState);             //[env, name]
        if (op == '=')
            childCodegen(node, 1, pState);      //[env, name, result]
        else
        {
            copyInstruction(1, pState);                         //[env, name, env]
            copyInstruction(1, pState);                         //[env, name, env, name]
            instruction8(OC_RD_FIELD, pState);                  //[env, name, lvalue];
            childCodegen(node, 1, pState);                      //[env, name, lvalue, rvalue];
            binaryOperatorCode (op, pState, node->position());  //[env, name, result];
        }
        instruction8(OC_WR_FIELD, pState);  //[result]
    }
}

/**
 * Generates code to write into a field
 * @param node
 * @param pState
 */
void fieldWriteCodegen (Ref<AstNode> node, CodegenState* pState)
{
    int     op = getAssignOp(node);
    auto    lexpr = node->children().front();
    auto    field = lexpr->children()[1]->getName();
    
    //Object access codegen
    childCodegen(lexpr, 0 , pState);        //[object]
    pushConstant(field, pState);            //[object, field]

    if (op == '=')
        childCodegen (node, 1, pState);     //[object, field, result]
    else
    {
        copyInstruction(1, pState);                         //[object, field, object]
        copyInstruction(1, pState);                         //[object, field, object, field]
        instruction8(OC_RD_FIELD, pState);                  //[object, field, lvalue];
        childCodegen(node, 1, pState);                      //[object, field, lvalue, rvalue];
        binaryOperatorCode (op, pState, node->position());  //[object, field, result];
    }
    instruction8(OC_WR_FIELD, pState);      //[result]
}


/**
 * Generates code to write an array element
 * @param node
 * @param pState
 */
void arrayWriteCodegen (Ref<AstNode> node, CodegenState* pState)
{
    int     op = getAssignOp(node);
    auto    lexpr = node->children().front();
    auto    field = lexpr->children()[1]->getName();
    
    //Array access and index codegen
    childCodegen(lexpr, 0, pState);         //[array]
    childCodegen(lexpr, 1, pState);         //[array, index]

    if (op == '=')
        childCodegen (node, 1, pState);     //[array, index, result]
    else
    {
        copyInstruction(1, pState);                         //[array, index, array]
        copyInstruction(1, pState);                         //[array, index, array, index]
        instruction8(OC_RD_INDEX, pState);                  //[array, index, lvalue];
        childCodegen(node, 1, pState);                      //[array, index, lvalue, rvalue];
        binaryOperatorCode (op, pState, node->position());  //[array, index, result];
    }
    instruction8(OC_WR_INDEX, pState);      //[result]
}


/**
 * Generates code for a function call
 * @param node
 * @param pState
 */
void fncallCodegen (Ref<AstNode> node, CodegenState* pState)
{
    const AstNodeTypes fnExprType = node->children()[0]->getType();

    //If the expression to get the function reference is an object member access,
    //then use generate a 'this' call.
    if (fnExprType == AST_MEMBER_ACCESS)
        thisCallCodegen (node, pState);
    else
    {
        //Parameters evaluation
        const int nChilds = (int)node->children().size();
        for (int i = 1; i < nChilds; ++i)
            childCodegen(node, i, pState);

        //Evaluate function reference expression
        childCodegen(node, 0, pState);

        callInstruction (nChilds-1, pState, node->position());
    }
}

/**
 * Generates code for function call which receives a 'this' reference.
 * @param node
 * @param pState
 */
void thisCallCodegen (Ref<AstNode> node, CodegenState* pState)
{
    auto fnExpr = node->children().front();
    
    //Parameters evaluation
    const int nChilds = (int)node->children().size();
    for (int i = 1; i < nChilds; ++i)
        childCodegen(node, i, pState);

                                            //[[params]]
    childCodegen(fnExpr, 0, pState);        //[this, [params]]
    instruction8 (OC_WR_THISP, pState);     //[this, [params]]
    
    const string  fnName = fnExpr->children()[1]->getName();
    pushConstant(fnName, pState);           //[fnName, this, [params]]
    instruction8(OC_RD_FIELD, pState);      //[function, [params]]
    
    callInstruction (nChilds-1, pState, node->position());
}

/**
 * Generates code for a literal expression.
 * @param node
 * @param pState
 */
void literalCodegen (Ref<AstNode> node, CodegenState* pState)
{
    pushConstant(node->getValue(), pState);
}

/**
 * Code generation for reading a variable.
 * Version which takes an AST node.
 * @param node
 * @param pState
 */
void varReadCodegen (Ref<AstNode> node, CodegenState* pState)
{
    return varReadCodegen(node->getName(), pState);
}

/**
 * Code generation for reading a variable.
 * Version which takes a string naming the variable
 * @param name
 * @param pState
 */
void varReadCodegen (const string& name, CodegenState* pState)
{
    ASSERT (!name.empty());
    
    if (name == "this")
    {
        instruction8(OC_PUSH_THIS, pState);                 //[thisPtr]
    }
    else if (pState->isDeclared(name))
    {
        if (!pState->isParam(name))
        {
            const int offset = pState->getLocalVarOffset(name);
            copyInstruction (offset, pState);               //[varValue]
        }
        else
        {
            const int index = pState->getParamIndex(name);
            pushConstant(jsInt(index), pState);             //[index]
            instruction8 (OC_RD_PARAM, pState);             //[paramValue]
        }
    }
    else
    {
        getEnvCodegen(pState);                  //[env]
        pushConstant(name, pState);             //[env, name]
        instruction8(OC_RD_FIELD, pState);      //[varValue]
    }
}

/**
 * Generates code for an array literal.
 * @param statement
 * @param pState
 */
void arrayCodegen (Ref<AstNode> statement, CodegenState* pState)
{
    const AstNodeList children = statement->children();
    
    pushConstant(0, pState);
    callCodegen("@newArray", 1, pState, statement->position()); //[array]
    instruction8(OC_CP, pState);        //[array, array]
    pushConstant("push", pState);       //["push", array, array]
    instruction8(OC_RD_FIELD, pState);  //[push, array]
    
    for (int i=0; i < (int)children.size(); ++i)
    {
        childCodegen(statement, i, pState); //[value, push, array]
        copyInstruction(1, pState);         //[push, value, push, array]
        copyInstruction(3, pState);         //[array, push, value, push, array]
        instruction8(OC_WR_THISP, pState);
        instruction8(OC_POP, pState);       //[push, value, push, array]
        callInstruction(1, pState, children[i]->position());    //[array, push, array]
        instruction8(OC_POP, pState);       //[push, array]
    }
    
    instruction8(OC_POP, pState);       //[array]
}

/**
 * Generates code for an object literal.
 * @param statement
 * @param pState
 */
void objectCodegen (Ref<AstNode> statement, CodegenState* pState)
{
    Ref<AstObject>                  obj = statement.staticCast<AstObject>();
    const AstObject::PropertyList   properties= obj->getProperties();
    
    callCodegen("Object", 0, pState, statement->position());

    //TODO: Properties are not evaluated in definition order, but in 
    //alphabetical order, as they are defined in a map.
    AstObject::PropertyList::const_iterator     it;
    for (it = properties.begin(); it != properties.end(); ++it)
    {
        copyInstruction (0, pState);        //Copy object reference
        pushConstant(it->name, pState);    //Property name
        codegen(it->expr, pState);        //Value expression
        
        const int opCode = it->isConst ? OC_NEW_CONST_FIELD : OC_WR_FIELD;
        instruction8(opCode, pState);
        instruction8(OC_POP, pState);
    }
    
    //After the loop, the object reference is on the top of the stack
}

/**
 * Generates code for array access operator ('[index]')
 * @param statement
 * @param pState
 */
void arrayAccessCodegen (Ref<AstNode> statement, CodegenState* pState)
{
    childrenCodegen(statement, pState);
    instruction8 (OC_RD_INDEX, pState);
}

/**
 * Generates code for member access operator ('object.field')
 * @param statement
 * @param pState
 */
void memberAccessCodegen(Ref<AstNode> statement, CodegenState* pState)
{
    childCodegen(statement, 0, pState);
    
    const string  fieldId = statement->children()[1]->getName();
    pushConstant(fieldId, pState);
    instruction8 (OC_RD_FIELD, pState);
}

/**
 * Generates code for the conditional operator.
 * @param statement
 * @param pState
 */
void conditionalCodegen (Ref<AstNode> statement, CodegenState* pState)
{
    //The conditional operator is handled by 'ifCodegen', as it is very similar.
    ifCodegen(statement, pState);    
}

/**
 * Generates code for a binary operators
 * @param statement
 * @param pState
 */
void binaryOpCodegen (Ref<AstNode> statement, CodegenState* pState)
{
    Ref<AstOperator>    op = statement.staticCast<AstOperator>();
    const int           opCode = op->code;
    
    if (opCode == LEX_OROR || opCode == LEX_ANDAND)
    {
        //Logical operators are special, because they have short-circuited evaluation
        //In all other cases, both sides of operation are evaluated.
        logicalOpCodegen (opCode, statement, pState);
    }
    else
    {
        childrenCodegen(statement, pState);
        binaryOperatorCode(opCode, pState, statement->position());
    }    
}

/**
 * Generates code for a prefix operator ('++' or '--')
 * @param node
 * @param pState
 */
void prefixOpCodegen(Ref<AstNode> node, CodegenState* pState)
{
    Ref<AstOperator>    op = node.staticCast<AstOperator>();
    const int           opCode = op->code;
    
    if (opCode == LEX_PLUSPLUS || opCode == LEX_MINUSMINUS)
    {
        //It is translated into a '+=' or '-=' equivalent operation
        Ref<AstLiteral> l = AstLiteral::create(op->position(), 1);
        const int       newOpCode = LEX_ASSIGN_BASE + (op->code == LEX_PLUSPLUS ? '+' : '-');
        Ref<AstNode>    newOp = astCreateAssignment(op->position(),
                                                    newOpCode,
                                                    op->children()[0], 
                                                    l);
        codegen(newOp, pState);
    }
    else if (opCode != '+')     //Plus unary operator does nothing, so no code is generated
    {
        childrenCodegen(node, pState);
        const char* function;
        
        switch (opCode)
        {
        case '-':       function = "@negate"; break;
        case '~':       function = "@binNot"; break;
        case '!':       function = "@logicNot"; break;
        default:
            ASSERT (!"Unexpected operator");
        }
        
        //Call function
        callCodegen(function, 1, pState, node->position());
    }
}

/**
 * Generates code for a prefix operator ('++' or '--')
 * @param node
 * @param pState
 */
void postfixOpCodegen (Ref<AstNode> node, CodegenState* pState)
{
    Ref<AstOperator>    opNode = node.staticCast<AstOperator>();
    const char*         fnName = opNode->code == LEX_MINUSMINUS ? "@inc" : "@dec";
    
    //Calls prefix code generation, and calls the opposite function to
    //recover the previous value.
    prefixOpCodegen(node, pState);                      //[inc-value]
    callCodegen(fnName, 1, pState,node->position());   //[prev-value]
}

/**
 * Generates code for a logical operator
 * @param opCode
 * @param statement
 * @param pState
 */
void logicalOpCodegen (const int opCode, Ref<AstNode> statement, CodegenState* pState)
{
    childCodegen(statement, 0, pState);
    copyInstruction (0, pState);
    const int firstBlock = curBlockId(pState);
    
    endBlock(firstBlock+1, firstBlock+1, pState);
    instruction8(OC_POP, pState);
    childCodegen(statement, 1, pState);
    copyInstruction (0, pState);
    const int secondBlock = curBlockId(pState);
    
    endBlock (secondBlock+1, secondBlock+1, pState);
    
    if (opCode == LEX_OROR)
    {
        setTrueJump(firstBlock, secondBlock+1, pState);
        setFalseJump(firstBlock, firstBlock+1, pState);
    }
    else
    {
        setTrueJump(firstBlock, firstBlock+1, pState);
        setFalseJump(firstBlock, secondBlock+1, pState);
    }
}

/**
 * Generates code for an actor declaration
 * @param node
 * @param pState
 */
void actorCodegen (Ref<AstNode> node, CodegenState* pState)
{
    errorAt (node->position(), "Actors code generation disabled temporarily");
//    auto  params = node->getParams();
//    CodegenState            actorState = initFunctionState(node, pState->pCodeMap);
//    
//    auto constructor = AsEndPoint::createInput("@start",
//                                               params,
//                                               actorState.curRoutine);
//    
//    childrenCodegen(node, &actorState);
//    
//    VarMap members = actorState.members;
//    members["@start"] = VarProperties(constructor, true);
//    
//    auto actor = AsActorClass::create(node->getName(), members, params);
//    
//    //Create a new constant, and yield actor class reference
//    pushConstant( actor, pState);
//    pushConstant(node->getName(), pState);
//    instruction8(OC_CP+1, pState);
//    instruction8(OC_NEW_CONST, pState);
}

/**
 * Generates code for 'connect' operator
 * @param node
 * @param pState
 */
void connectCodegen (Ref<AstNode> node, CodegenState* pState)
{
    errorAt (node->position(), "Actors code generation disabled temporarily");
//    pushConstant("this", pState);
//    instruction8(OC_RD_LOCAL, pState);
//    pushConstant(node->children().front()->getName(), pState);
//    instruction8(OC_RD_FIELD, pState);
//    childCodegen(node, 1, pState);
//    callCodegen("@connect", 2, pState, node->position());
}

/**
 * Generates code for a message declared inside an actor.
 * @param node
 * @param pState
 */
void messageCodegen (Ref<AstNode> node, CodegenState* pState)
{
    errorAt (node->position(), "Actors code generation disabled temporarily");
//    Ref<AsEndPoint>  msg = AsEndPoint::create(node->getName(), 
//                                              node->getParams(), 
//                                              node->getType() == AST_INPUT);
//
//    if (node->getType() == AST_INPUT)
//    {
//        //TODO: This code is very simular in function, actor, and message code generation
//        Ref<MvmRoutine>         code = MvmRoutine::create();
//
//        CodegenState    fnState = initFunctionState(node, pState->pCodeMap);
//        msg->setCodeMVM (code);
//        fnState.curRoutine = code;
//
//        codegen (node.staticCast<AstFunction>()->getCode(), &fnState);
//    }
//
//    pState->members[msg->getName()] = VarProperties(msg, true);
}

/**
 * Generates code which clears the local variables created in a block of code.
 * @param targetStackSize
 * @param pState
 */
void clearLocals (int targetStackSize, CodegenState* pState)
{
    int nVars = pState->stackSize - targetStackSize;
    ASSERT (nVars >= 0);
    for (; nVars > 0; --nVars)
        instruction8(OC_POP, pState);
}


/**
 * Generates code for class definitions
 * @param node
 * @param pState
 */
void classCodegen (Ref<AstNode> node, CodegenState* pState)
{
    auto    constructorFn = classConstructorCodegen(node, pState);
    auto    children = node->children();
    VarMap  members;
    
    for (auto it = children.begin(); it != children.end(); ++it)
    {
        auto child = *it;
        
        if (child.notNull() && child->getType() == AST_FUNCTION)
        {
            auto function = createFunction(child, pState);
            members.checkedVarWrite (function->getName(), function->value(), true);
        }
    }
    
    auto parentClass = getParentClass (node, pState);
    auto cls = JSClass::create(node->getName(), parentClass, members, constructorFn);
    
    //Register class
    pState->symbols[node->getName()] = cls->value();
    
    //Create a new constant, and yield class reference
    getEnvCodegen(pState);                  //[env]
    copyInstruction(0, pState);             //[env, env]
    pushConstant(node->getName(), pState);  //[name, env, env]
    pushConstant( cls->value(), pState);    //[class, name, env, env]
    instruction8(OC_NEW_CONST_FIELD, pState);//[class, env]
    
    //Set environment.
    callCodegen("@setClassEnv", 2, pState, node->position());   //[class]
}

/**
 * Generates the code for a class constructor function.
 * @param node
 * @param pState
 * @return 
 */
Ref<JSFunction> classConstructorCodegen (Ref<AstNode> node, CodegenState* pState)
{
    auto            params = classConstructorParams(node, pState);
    CodegenState    fnState = initFunctionState(node, params, pState->pCodeMap);

    auto            function = JSFunction::createJS("", params, fnState.curRoutine);
    auto            children = node->children();
    set<string>     vars;
    
    pState = &fnState;
    baseConstructorCallCodegen (node, pState);      //[newObj]
    
    //Set object class. First parameter of constructor closure.
    getEnvCodegen(pState);                          //[env,newObj]
    pushConstant(0, pState);                        //[0, env,newObj]
    instruction8(OC_RD_INDEX, pState);              //[class, newObj]
    callCodegen ("@setObjClass", 2, pState, node->position());  //[newObj]
    
    for (auto it = children.begin(); it != children.end(); ++it)
    {
        auto child = *it;
        
        if (child.notNull())
        {
            auto type = child->getType();
            
            if (type == AST_VAR || type == AST_CONST)
            {
                instruction8(OC_CP, pState);            //[newObj, newObj]
                pushConstant (child->getName(), pState);//[name, newObj, newObj]
                if (!childCodegen(child, 0, pState))    //[value, name, newObj, newObj]
                    pushConstant (jsNull(), pState);

                const int opCode = type == AST_CONST ? OC_NEW_CONST_FIELD : OC_WR_FIELD;
                instruction8 (opCode, pState);          //[value, newObj]
                instruction8 (OC_POP, pState);          //[newObj]
                
                vars.insert(child->getName());
            }
        }
    }//class members

    //Stack:[newObj]

    //Parameters as class variables
    for (auto itParam = params.begin(); itParam != params.end(); ++itParam)
    {
        //Check if already written.
        if (vars.count(*itParam) == 0)
        {
            instruction8(OC_CP, pState);        //[newObj, newObj]
            
            pushConstant (*itParam, pState);    //[paramName, newObj, newObj]
            varReadCodegen(*itParam, pState);   //[value, paramName, newObj, newObj]
            
            instruction8 (OC_WR_FIELD, pState); //[value, newObj]
            instruction8 (OC_POP, pState);      //[newObj]
        }
    }
    
    //Stack:[newObj]

    return function;
}

/**
 * Generates the code which calls the base class constructor.
 * @param node
 * @param pState
 */
void baseConstructorCallCodegen (Ref<AstNode> node, CodegenState* pState)
{
    auto    parentClass = getParentClass(node, pState);
    auto    extends = astGetExtends (node);
    
    int     nParams;
    
    if (extends.notNull() && extends->childExists(0))
    {
        //Non-inherited parameters
        auto paramsNode = extends->children().front();
        nParams = childrenCodegen(paramsNode, pState);
    }
    else
    {
        //inherited parameters
        StringVector    params = parentClass->getParams();

        nParams = (int)params.size();
        for (int i=0; i<nParams; ++i)
        {
            varReadCodegen(params[i], pState);
        }
    }

    callCodegen(parentClass->getName(), nParams, pState, node->position());
}


/**
 * Gets class constructor parameter list, taking into account parameter inheritance rules.
 * @param node
 * @param pState
 * @return 
 */
StringVector classConstructorParams(Ref<AstNode> node, CodegenState* pState)
{
    auto    extends = astGetExtends (node);
    
    if (extends.isNull())
        return node->getParams();

    if (extends->childExists(0))
        return node->getParams();   //Non-inherited parameters
    else
    {
        //inherited parameters
        auto            parentClass = getParentClass (node, pState);
        StringVector    params = parentClass->getParams();
        auto            nodeParams = node->getParams();
        
        params.insert(params.end(), nodeParams.begin(), nodeParams.end());
        return params;
    }
}

/**
 * Looks for a reference of the parent class in current scope.
 * By the moment is limited to search in the current constants
 * @param node
 * @param pState
 * @return 
 */
Ref<JSClass> getParentClass (Ref<AstNode> node, CodegenState* pState)
{
    auto extends = astGetExtends(node);
    
    if (extends.isNull())
        return JSObject::DefaultClass;
    
    auto parentName = extends->getName();
    auto itParent = pState->symbols.find(parentName);
    
    if (itParent == pState->symbols.end())
        errorAt(node->position(), "Parent class '%s' does not exist", parentName.c_str());
    
    auto parentClass = itParent->second;
    
    if (parentClass.getType() != VT_CLASS)
        errorAt(node->position(), "'%s' is not a class", parentName.c_str());
    
    return parentClass.staticCast<JSClass>();
}

/**
 * Generates the code for an 'export' modifier.
 * @param node
 * @param pState
 */
void exportCodegen (Ref<AstNode> node, CodegenState* pState)
{
    //It just generates the code for the child node, and exports the value which
    //it yields.
    auto child = node->children().front();
    string name = child->getName();
    
    if (name.empty ())
        errorAt (child->position(), "Cannot export an unnamed symbol");
    
    childCodegen(node, 0, pState);      //[value]
    pushConstant(name, pState);         //[name, value]
    getEnvCodegen(pState);              //[env, name, value]
    
    callCodegen("@exportSymbol", 2, pState, node->position());  //[env, value]
    instruction8(OC_POP, pState);       //[value]
}

/**
 * Generates code for an import module statement.
 * @param node
 * @param pState
 */
void importCodegen (Ref<AstNode> node, CodegenState* pState)
{
    childCodegen (node, 0, pState);         //[importPath]
    getEnvCodegen(pState);                  //[env, importPath]
    
    callCodegen("@importModule", 2, pState, node->position());  //[null]
}


/**
 * Generates the instruction which pushes a constant form the constant table into
 * the stack
 * @param value
 * @param pState
 */
void pushConstant (ASValue value, CodegenState* pState)
{
    //TODO: It can be further optimized, by deduplicating constants at script level.
    int                             id = (int)pState->curRoutine->constants.size();
    ConstantsMap::const_iterator    it = pState->constants.find(value);
    
    if (it != pState->constants.end())
        id = it->second;
    else
    {
        pState->constants[value] = id;
        pState->curRoutine->constants.push_back(value);
    }
    
    if (id < OC_PUSHC)
        instruction8(OC_PUSHC + id, pState);
    else
    {
        const int id16 = id - OC_PUSHC;

        //TODO: 32 bit instructions, to have more constants.
        if (id16 >= OC16_PUSHC)
            errorAt (pState->curPos,  "Too much constants. Maximum is 8256 per function");
        else
            instruction16(id16 + OC16_PUSHC, pState);
    }
}

/**
 * Push string constant
 * @param str
 * @param pState
 */
void pushConstant (const char* str, CodegenState* pState)
{
    pushConstant(jsString(str), pState);
}

/**
 * Push string constant
 * @param str
 * @param pState
 */
void pushConstant (const std::string& str, CodegenState* pState)
{
    pushConstant(jsString(str), pState);
}

/**
 * Push integer constant
 * @param value
 * @param pState
 */
void pushConstant (int value, CodegenState* pState)
{
    pushConstant(jsInt(value), pState);
}

/**
 * Push boolean constant
 * @param value
 * @param pState
 */
void pushConstant (bool value, CodegenState* pState)
{
    pushConstant(jsBool(value), pState);
}

/**
 * Pushes an 'null' value into the stack
 * @param pState
 */
void pushNull (CodegenState* pState)
{
    pushConstant(jsNull(), pState);
}

/**
 * Adds the instructions necessary to perform a call to a function, given its name.
 * @param fnName
 * @param nParams
 * @param pState
 * @param pos       Source code position
 */
void callCodegen (const std::string& fnName, 
                  int nParams, 
                  CodegenState* pState, 
                  const ScriptPosition& pos)
{
    //errorAt (pos, "Function call code generation disabled temporarily");
    const auto oldPos = pState->curPos;
    pState->curPos = pos;
    
    varReadCodegen (fnName, pState);
    callInstruction(nParams, pState, pos);
    
    pState->curPos = oldPos;
}


/**
 * Writes a call instruction. Takes into account the number of parameters to 
 * create the 8 bit or 16 version.
 * @param nParams Number of parameters, including this pointer (which is the first one)
 * @param pState
 * @param pos       Source code position
 */
void callInstruction (int nParams, CodegenState* pState, const ScriptPosition& pos)
{
    const auto oldPos = pState->curPos;
    pState->curPos = pos;

    if (nParams <= OC_CALL_MAX)
        instruction8(OC_CALL + nParams, pState);
    else {
        //I think that 1031 arguments are enough.
        if (nParams > (OC_CALL_MAX + OC16_CALL_MAX + 1))
            errorAt(pos, "Too much arguments in function call: %d", nParams);
        
        //TODO: Provide location for the error.
        
        instruction16(OC16_CALL + nParams - (OC_CALL_MAX+1), pState);
    }
    pState->curPos = oldPos;
}

/**
 * Generates an 8 bit or 16 copy instruction, depending on the offset
 * @param offset
 * @param pState
 */
void copyInstruction (int offset, CodegenState* pState)
{
    ASSERT (offset >= 0);
    if (offset <= OC_CP_MAX - OC_CP)
        instruction8 (OC_CP + offset, pState);
    else
    {
        offset -= (OC_CP_MAX - OC_CP) + 1;
        if (offset > OC16_CP_MAX - OC16_CP)
            errorAt(pState->curPos, "Cannot generate copy instruction: Too much locals. Try to simplify the function");
        instruction16 (OC16_CP + offset, pState);
    }
}

/**
 * Generates an 8 bit or 16 'write' instruction, depending on the offset
 * @param offset
 * @param pState
 */
void writeInstruction (int offset, CodegenState* pState)
{
    ASSERT (offset >= 0);
    if (offset <= OC_WR_MAX - OC_WR)
        instruction8 (OC_WR + offset, pState);
    else
    {
        offset -= (OC_WR_MAX - OC_WR) + 1;
        if (offset > OC16_WR_MAX - OC16_WR)
            errorAt(pState->curPos, "Cannot generate write instruction: Too much locals. Try to simplify the function");
        instruction16 (OC16_WR + offset, pState);
    }
}

/**
 * Writes a 8 bit instruction to the current block
 * @param opCode
 * @param pState
 */
void instruction8 (int opCode, CodegenState* pState)
{
    ASSERT (opCode < 128 && opCode >=0);
    
    const auto routine = pState->curRoutine;
    
    routine->blocks.rbegin()->instructions.push_back(opCode);
    
    if (pState->pCodeMap != NULL)
    {
        const int blockIdx = (int)routine->blocks.size()-1;
        VmPosition  pos (routine, blockIdx, (int)routine->blocks[blockIdx].instructions.size()-1);
        pState->pCodeMap->add(pos, pState->curPos);
    }
    
    //Update stack position
    pState->stackSize += calcStackOffset8(opCode);
    
    //printf ("\nopCode: %02X stack: %u", opCode, (unsigned)pState->stackSize);
}

/**
 * Writes a 16 bit instruction to the current block
 * @param opCode
 * @param pState
 */
void instruction16 (int opCode, CodegenState* pState)
{
    ASSERT (opCode >=0 && opCode < 0x8000);
    
    opCode |= 0x8000;   //16 bits indicator
    
    ByteVector&     block = pState->curRoutine->blocks.rbegin()->instructions;
    
    block.push_back((unsigned char)(opCode >> 8));
    block.push_back((unsigned char)(opCode & 0xff));
    
    if (pState->pCodeMap != NULL)
    {
        const auto routine = pState->curRoutine;
        const int blockIdx = (int)routine->blocks.size()-1;
        VmPosition  pos (routine, blockIdx, (int)block.size()-2);
        pState->pCodeMap->add(pos, pState->curPos);
    }
    
    //Update stack position
    pState->stackSize += calcStackOffset16(opCode);
}

/**
 * Returns last instruction of the current block.
 * @param pState
 * @return 
 */
int  getLastInstruction (CodegenState* pState)
{
    const ByteVector& block = pState->curRoutine->blocks.rbegin()->instructions;
    
    if (block.size() > 1)
    {
        const unsigned char     last = *block.rbegin();
        const unsigned char     prev = *(block.rbegin()+1);
        
        if (prev & OC_EXT_FLAG)
        {
            const int instruction = (int(prev)<< 8) + int(last);
            return instruction;
        }
        else
        {
            ASSERT(!(last & OC_EXT_FLAG));
            return last;
        }
    }
    else if (block.size() == 1)
        return block[0];
    else
        return -1;
        
}

/**
 * Generates the instruction or call for a binary operator
 * @param tokenCode
 * @param pState
 * @param pos
 */
void binaryOperatorCode (int tokenCode, CodegenState* pState, const ScriptPosition& pos)
{
    typedef map <int, string> OpMap;
    static OpMap operators;
    
    if (operators.empty())
    {
        operators['+'] =                "@add";
        operators['-'] =                "@sub";
        operators['*'] =                "@multiply";
        operators['/'] =                "@divide";
        operators['%'] =                "@modulus";
        operators[LEX_POWER] =          "@power";
        operators['&'] =                "@binAnd";
        operators['|'] =                "@binOr";
        operators['^'] =                "@binXor";
        operators[LEX_LSHIFT] =         "@lshift";
        operators[LEX_RSHIFT] =         "@rshift";
        operators[LEX_RSHIFTUNSIGNED] = "@rshiftu";
        operators['<'] =                "@less";
        operators['>'] =                "@greater";
        operators[LEX_EQUAL] =          "@areEqual";
        operators[LEX_TYPEEQUAL] =      "@areTypeEqual";
        operators[LEX_NEQUAL] =         "@notEqual";
        operators[LEX_NTYPEEQUAL] =     "@notTypeEqual";
        operators[LEX_LEQUAL] =         "@lequal";
        operators[LEX_GEQUAL] =         "@gequal";
    }
    
    OpMap::const_iterator it = operators.find(tokenCode);
    
    ASSERT (it != operators.end());
    callCodegen(it->second, 2, pState, pos);
}

/**
 * Generates the code which reads the environment pointer
 * @param pState
 */
void getEnvCodegen (CodegenState* pState)
{
    copyInstruction(pState->stackSize, pState);
}

/**
 * Ends current block, and creates a new one.
 * @param trueJump      Jump target when the value on the top of the stack is 'true'
 * @param falseJump     Jump target when the value on the top of the stack is 'false'
 * @param pState
 */
void endBlock (int trueJump, int falseJump, CodegenState* pState)
{
    MvmBlock    &curBlock = pState->curRoutine->blocks.back();
    
    curBlock.nextBlocks[1] = trueJump;
    curBlock.nextBlocks[0] = falseJump;
    
    if (trueJump >= 0)
        --pState->stackSize;

    pState->curRoutine->blocks.push_back(MvmBlock());
}

/**
 * Sets the jump target for the specified block, in 'true' case
 * @param blockId
 * @param destinationId
 * @param pState
 */
void setTrueJump (int blockId, int destinationId, CodegenState* pState)
{
    pState->curRoutine->blocks[blockId].nextBlocks[1] = destinationId;
}

/**
 * Sets the jump target for the specified block, in 'false' case
 * @param blockId
 * @param destinationId
 * @param pState
 */
void setFalseJump (int blockId, int destinationId, CodegenState* pState)
{
    pState->curRoutine->blocks[blockId].nextBlocks[0] = destinationId;
}

/**
 * Returns current block id.
 */
int curBlockId (CodegenState* pState)
{
    return (int)pState->curRoutine->blocks.size() - 1;
}

/**
 * Given an assignment operation node, it returns:
 * - '=' if it is a simple assignment
 * - The binary operator if it is a combination of binary operation + assignment.
 * One of those: '+', '-', '*'...
 * @param node
 * @return 
 */
int  getAssignOp (Ref<AstNode> node)
{
    Ref<AstOperator>  assign = node.staticCast<AstOperator>();
    const int         op = assign->code;
    
    if (op == '=')
        return op;
    else
        return op - LEX_ASSIGN_BASE;
}

/**
 * Calculates the stack size variation caused by an 8-bit instruction.
 * @param opCode
 * @return 
 */
int calcStackOffset8(int opCode)
{
    if (opCode <= OC_CALL_MAX)
        return -opCode;
    else if (opCode <= OC_CP_MAX)
        return 1;
    else if (opCode >= OC_PUSHC)
        return 1;
    else
    {
        switch (opCode)
        {
        case OC_POP:        return -1;
        case OC_RD_FIELD:   return -1;    
        case OC_RD_INDEX:   return -1;    
        case OC_WR_FIELD:   return -2;    
        case OC_WR_INDEX:   return -2;  
        case OC_NEW_CONST_FIELD:   return -2;
        case OC_WR_PARAM:   return -1;
        case OC_NUM_PARAMS: return 1;
        case OC_PUSH_THIS:  return 1;
        default:            return 0;
        }
    }
    
}

/**
 * Calculates the stack size variation caused by an 16-bit instruction.
 * @param opCode
 * @return 
 */
int calcStackOffset16(int opCode)
{
    opCode &= ~OC16_16BIT_FLAG;

    if (opCode <= OC16_CALL_MAX)
        return -(opCode + OC_CALL_MAX + 1);
    else if (opCode <= OC16_CP_MAX)
        return 1;
    else if (opCode >= OC16_PUSHC)
        return 1;
    else
        return 0;
}



/**
 * Initializes a 'CodegenState' object for a function
 * @param node
 * @return 
 */
CodegenState initFunctionState (Ref<AstNode> node, CodeMap* pMap)
{
    return initFunctionState(node, node->getParams(), pMap);
}

/**
 * Initializes a 'CodegenState' object for a function
 * @param node
 * @param params
 * @return 
 */
CodegenState initFunctionState (Ref<AstNode> node, const StringVector& params, CodeMap* pMap)
{
    Ref<MvmRoutine>         code = MvmRoutine::create();
    CodegenState            fnState;
    
    fnState.curPos = node->position();
    fnState.pCodeMap = pMap;
    fnState.curRoutine = code;
    fnState.pushScope(node, false, true);

    //Declare function reserved symbols.
    fnState.declare("this");
//    fnState.declare("arguments");

    for (size_t i = 0; i < params.size(); ++i)
    {
        fnState.declare(params[i]);
        ++fnState.stackSize;
    }
    fnState.stackSize = 0;

    return fnState;
}
