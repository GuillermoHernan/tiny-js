/* 
 * File:   semanticCheck.cpp
 * Author: ghernan
 * 
 * Semantic check code for AsyncScript
 * 
 * Created on December 17, 2016, 10:54 PM
 */

#include "ascript_pch.hpp"
#include "OS_support.h"
#include "semanticCheck.h"
#include "ScriptException.h"

#include <string>
#include <set>
#include <vector>

using namespace std;

/**
 * State of the semantic analysis process
 */
struct SemCheckState
{    
    StringSet               definedClasses;
    vector<Ref<AstNode> >   nodeStack;
    
    bool isClassDefined (const std::string& name)const
    {
        return definedClasses.find(name) != definedClasses.end();
    }
};

//Forward declarations

typedef void (*SemcheckFN)(Ref<AstNode> node, SemCheckState* pState);

void semCheck (Ref<AstNode> node, SemCheckState* pState);
void childrenSemCheck (Ref<AstNode> node, SemCheckState* pState);
void varSemCheck (Ref<AstNode> node, SemCheckState* pState);
void functionSemCheck (Ref<AstNode> node, SemCheckState* pState);
void assignmentSemCheck (Ref<AstNode> node, SemCheckState* pState);
void postfixOpSemCheck (Ref<AstNode> node, SemCheckState* pState);
void prefixOpSemCheck (Ref<AstNode> node, SemCheckState* pState);
void objectSemCheck (Ref<AstNode> node, SemCheckState* pState);
void classSemCheck (Ref<AstNode> node, SemCheckState* pState);
void exportSemCheck (Ref<AstNode> node, SemCheckState* pState);
void importSemCheck (Ref<AstNode> node, SemCheckState* pState);

void checkReservedNames (const std::string& name, ScriptPosition pos, const char* errorMsg);
void checkReservedNames (Ref<AstNode> node, const char* errorMsg);

Ref<AstNode> getPrevSibling(Ref<AstNode> node, SemCheckState* pState);
Ref<AstNode> getParent(SemCheckState* pState);
AstNodeTypes getParentType(SemCheckState* pState);


/**
 * Check:
 * - Var declaration
 * - Function name
 * - Assignment
 * - Function parameter names
 * - PostFix and prefix increment operators
 */

/**
 * Semantic analysis pass entry point. It throws a 'CScriptException' on the first
 * error it finds.
 * @param script
 */
void semanticCheck(Ref<AstNode> script)
{
    ASSERT (script->getType() == AST_SCRIPT);
    
    SemCheckState   state;
    
    state.nodeStack.push_back(script);
    childrenSemCheck(script, &state);
}

/**
 * Performs semantic analysis on an AST node.
 * @param node
 * @param pState
 */
void semCheck (Ref<AstNode> node, SemCheckState* pState)
{
    static SemcheckFN types[AST_TYPES_COUNT] = {NULL, NULL};
    
    if (types [0] == NULL)
    {
        types [AST_SCRIPT] = childrenSemCheck;
        types [AST_BLOCK] = childrenSemCheck;
        types [AST_VAR] = varSemCheck;
        types [AST_CONST] = varSemCheck;
        types [AST_IF] = childrenSemCheck;
        types [AST_FOR] = childrenSemCheck;
        types [AST_FOR_EACH] = childrenSemCheck;
        types [AST_RETURN] = childrenSemCheck;
        types [AST_FUNCTION] = functionSemCheck;
        types [AST_ASSIGNMENT] = assignmentSemCheck;
        types [AST_FNCALL] = childrenSemCheck;
//        types [AST_NEWCALL] = childrenSemCheck;
        types [AST_LITERAL] = childrenSemCheck;
        types [AST_IDENTIFIER] = childrenSemCheck;
        types [AST_ARRAY] = childrenSemCheck;
        types [AST_OBJECT] = objectSemCheck;
        types [AST_ARRAY_ACCESS] = childrenSemCheck;
        types [AST_MEMBER_ACCESS] = childrenSemCheck;
        types [AST_CONDITIONAL] = childrenSemCheck;
        types [AST_BINARYOP] = childrenSemCheck;
        types [AST_PREFIXOP] = prefixOpSemCheck;
        types [AST_POSTFIXOP] = postfixOpSemCheck;
        types [AST_ACTOR] = childrenSemCheck;
        types [AST_CONNECT] = childrenSemCheck;
        types [AST_INPUT] = childrenSemCheck;
        types [AST_OUTPUT] = childrenSemCheck;
        types [AST_CLASS] = classSemCheck;
        types [AST_EXTENDS] = childrenSemCheck;
        types [AST_EXPORT] = exportSemCheck;
        types [AST_IMPORT] = importSemCheck;
    }

    pState->nodeStack.push_back(node);
    types[node->getType()](node, pState);
    
    ASSERT(!pState->nodeStack.empty());
    pState->nodeStack.pop_back();
}

/**
 * Performs semantic analysis on every children of the current AST node.
 * @param node
 * @param pState
 * @return number of children nodes processed
 */
void childrenSemCheck (Ref<AstNode> node, SemCheckState* pState)
{
    const AstNodeList&  children = node->children();
    
    for (size_t i=0; i < children.size(); ++i)
    {
        if (children[i].notNull())
            semCheck (children[i], pState);
    }
}

/**
 * Semantic analysis for 'var' declarations
 * @param node
 * @param pState
 */
void varSemCheck (Ref<AstNode> node, SemCheckState* pState)
{
    checkReservedNames (node, "Invalid variable name");
    childrenSemCheck(node, pState);
}

/**
 * Semantic analysis for function definition nodes.
 * @param node
 * @param pState
 */
void functionSemCheck (Ref<AstNode> node, SemCheckState* pState)
{
    Ref<AstFunction> fnNode = node.staticCast<AstFunction>();

    checkReservedNames (node, "Invalid function name");
    
    auto params = fnNode->getParams();
    for (size_t i = 0; i < params.size(); ++i)
        checkReservedNames (params[i], node->position(), "Invalid parameter name: %s");
    
    SemCheckState   fnState (*pState);
    semCheck (fnNode->getCode(), &fnState);
}

/**
 * Semantic analysis for assignment nodes
 * @param node
 * @param pState
 */
void assignmentSemCheck (Ref<AstNode> node, SemCheckState* pState)
{
    checkReservedNames (node->children().front(), "Cannot write to: %s");

    childrenSemCheck(node, pState);
}

/**
 * Semantic analysis for assignment nodes
 * @param node
 * @param pState
 */
void postfixOpSemCheck (Ref<AstNode> node, SemCheckState* pState)
{
    checkReservedNames (node->children().front(), "Cannot write to: %s");

    childrenSemCheck(node, pState);
}

/**
 * Semantic analysis for assignment nodes
 * @param node
 * @param pState
 */
void prefixOpSemCheck (Ref<AstNode> node, SemCheckState* pState)
{
    const auto  opNode = node.staticCast<AstOperator>();
    const auto  opCode = opNode->code;
    
    if (opCode == LEX_PLUSPLUS || opCode == LEX_MINUSMINUS )
        checkReservedNames (node->children().front(), "Cannot write to: %s");

    childrenSemCheck(node, pState);
}

void objectSemCheck(Ref<AstNode> node, SemCheckState* pState)
{
    Ref<AstObject> objNode = node.staticCast<AstObject>();

    auto        props = objNode->getProperties();
    set<string> usedNames;
    
    for (size_t i = 0; i < props.size(); ++i)
    {
        if (usedNames.count(props[i].name) == 0)
            usedNames.insert(props[i].name);
        else
        {
            errorAt (props[i].expr->position(), 
                     "Duplicated key in object: %s", 
                     props[i].name.c_str());
        }
    }
}

/**
 * Semantic check for class definitions.
 * @param node
 * @param pState
 */
void classSemCheck (Ref<AstNode> node, SemCheckState* pState)
{
    auto classNode = node.staticCast<AstClassNode>();
    
    auto name = classNode->getName();

    checkReservedNames (node, "Invalid class name");

    if (pState->isClassDefined(name))
        errorAt(node->position(), "'%s' class already defined. Use another name", name.c_str());
    
    auto extends = classNode->getExtendsNode();
    if (extends.notNull())
    {
        auto parentName = extends->getName();

        if (!pState->isClassDefined(parentName))
            errorAt(extends->position(), "Base class '%s' not defined.", parentName.c_str());
    }
    
    SemCheckState   classState (*pState);
    childrenSemCheck(node, &classState);
}

/**
 * Semantic check for 'export' modifiers.
 * @param node
 * @param pState
 */
void exportSemCheck (Ref<AstNode> node, SemCheckState* pState)
{
    if (getParentType(pState) != AST_SCRIPT)
        errorAt(node->position(), "Exported symbols must be globals");
}

/**
 * Semantic check for 'import' statements.
 * @param node
 * @param pState
 */
void importSemCheck (Ref<AstNode> node, SemCheckState* pState)
{
    if (getParentType(pState) != AST_SCRIPT)
        errorAt(node->position(), "Import statements must be top-level elements");

    auto prev = getPrevSibling(node, pState);
    
    if (prev.notNull() && prev->getType() != AST_IMPORT)
        errorAt(node->position(), "Import statements must come before any other statements");
}

/**
 * Checks that the name is not among the reserved names
 * @param name
 * @param pos
 * @param errorMsg
 */
void checkReservedNames (const std::string& name, ScriptPosition pos, const char* errorMsg)
{
    static set <string>     reserved;
    
    if (reserved.empty())
    {
        //TODO: probably 'this' would need more checks.
        reserved.insert("this");
        reserved.insert("arguments");
        reserved.insert("eval");
    }
    
    if (reserved.count(name) > 0)
        errorAt(pos, errorMsg, name.c_str());
}

/**
 * Checks that node name is not among reserved names.
 * @param node
 * @param errorMsg
 */
void checkReservedNames (Ref<AstNode> node, const char* errorMsg)
{
    checkReservedNames(node->getName(), node->position(), errorMsg);
}

/**
 * Gets the previous sibling, if any.
 *
 * @param pState
 * @return If there is no previous sibling, it will return null.
 */
Ref<AstNode> getPrevSibling(Ref<AstNode> node, SemCheckState* pState)
{
    auto parent = getParent(pState);
    
    if (parent.notNull())
    {
        const auto &children = parent->children();
        
        for (size_t i = 0; i < children.size(); ++i)
        {
            if (node == children[i])
            {
                if (i > 0)
                    return children[i-1];
                else
                    break;
            }
        }
    }
    
    return Ref<AstNode>();
}

/**
 * Gets the parent node of the current node.
 * @param pState
 * @return 
 */
Ref<AstNode> getParent(SemCheckState* pState)
{
    if (pState->nodeStack.size() > 1)
        return *(pState->nodeStack.rbegin()+1);
    else
        return Ref<AstNode>();
}

/**
 * Gets the type of the parent node.
 * @param pState
 * @return If the current node has no parent, it returns AST_TYPES_COUNT.
 */
AstNodeTypes getParentType(SemCheckState* pState)
{
    auto parent = getParent(pState);
    
    if (parent.notNull())
        return parent->getType();
    else
        return AST_TYPES_COUNT;
}
