/* 
 * File:   scriptMain.cpp
 * Author: ghernan
 * 
 * Script engine main file. Contains 'evaluate' function, which runs an script
 * contained in a string.
 * 
 * Created on December 4, 2016, 10:31 PM
 */

#include "OS_support.h"
#include "scriptMain.h"
#include "jsParser.h"
#include "mvmCodegen.h"
#include "TinyJS_Functions.h"
#include "TinyJS_MathFunctions.h"
#include "mvmFunctions.h"

using namespace std;

// Functions forward declarations.
//////////////////////////////////////////
CScriptToken parseFunctionArguments(Ref<JSFunction> function, CScriptToken token);

/**
 * Script evaluation function. Runs a script, and returns its result.
 *
 * @param script    Script code, in a C string.
 * @param globals   Global symbols
 * @return 
 */
Ref<JSValue> evaluate (const char* script, Ref<IScope> globals)
{
    CScriptToken    token (script);
    StatementList   statements;

    //Parsing loop
    token = token.next();
    while (!token.eof())
    {
        const ParseResult   parseRes = parseStatement (token);
        
        statements.push_back(parseRes.ast);
        token = parseRes.nextToken;
    }
    
    //Code generation.
    const Ref<MvmScript>    code = scriptCodegen(statements);
    
    //Execution
    return mvmExecute(code, globals);
}

/**
 * Creates the default global scope
 * @return 
 */
Ref<IScope> createDefaultGlobals()
{
    Ref<JSObject>   globals = createDefaultGlobalsObj();
    
    return ObjectScope::create(globals);
}

/**
 * Creates the default global object, which backs the default global scope
 * @return 
 */
Ref<JSObject> createDefaultGlobalsObj()
{
    Ref<JSObject>   globals = JSObject::create(Ref<JSObject>());
    Ref<IScope>     tmpScope = ObjectScope::create(globals);
    
    registerMvmFunctions(tmpScope);
    registerFunctions(tmpScope);
    registerMathFunctions(tmpScope);
    
    return globals;
}

/**
 * Adds a new native function. This version uses a javascript declaration of
 * the function name to get its name and parameters.
 * @param szFunctionHeader  Javascript function header definition, which includes the
 * function name and parameters.
 * @param pFn               Pointer to native function.
 * @param scope             Scope object to which the function will be added.
 * @return A new Javascript function object
 */
Ref<JSFunction> addNative (const std::string& szFunctionHeader, 
                           JSNativeFn pFn, 
                           Ref<IScope> scope)
{
    CScriptToken token(szFunctionHeader.c_str());
    token = token.next();

    token = token.match(LEX_R_FUNCTION);
    string funcName = token.text();
    token = token.match(LEX_ID);

    // Check for dots, we might want to do something like function String.substring ...
    while (token.type() == '.')
    {
        token = token.match('.');
        Ref<JSObject> child = getObject(scope, funcName);

        // if it doesn't exist, make an object class
        if (child.isNull())
        {
            child = JSObject::create( JSObject::DefaultPrototype );
            scope->set(funcName, child);
        }

        scope = ObjectScope::create(child);
        funcName = token.text();
        token = token.match(LEX_ID);
    }

    Ref<JSFunction> function = JSFunction::createNative(funcName, pFn);
    parseFunctionArguments(function, token);

    scope->set(funcName, function);
    
    return function;
}


/**
 * Parses a function argument list.
 * @note Used by 'addNative' function
 * @param function  Function into which add the arguments
 * @param token 
 * @return Next token
 */
CScriptToken parseFunctionArguments(Ref<JSFunction> function, CScriptToken token)
{
    //TODO: It is a copy & paste from the function of the same name in 'jsParser.cpp'
    //But the other version uses a 'AstFunction', not a 'JSFunction'
    token = token.match('(');

    while (token.type() != ')')
    {
        const string name = token.text();

        token = token.match(LEX_ID);
        function->addParam(name);

        if (token.type() != ')')
            token = token.match(',');
    }
    return token.match(')');
}


/**
 * Adds a native function with no arguments
 * @param szName    Function name
 * @param pFn       Pointer to native function
 * @param scope     Scope object to which the function will be added.
 * @return A new Javascript function object
 */
Ref<JSFunction> addNative0 (const std::string& szName, 
                           JSNativeFn pFn, 
                           Ref<IScope> scope)
{
    Ref<JSFunction> function = JSFunction::createNative(szName, pFn);

    scope->set(szName, function);
    
    return function;
    
}

/**
 * Adds a native function with one argument
 * @param szName    Function name
 * @param p1        First parameter name
 * @param pFn       Pointer to native function
 * @param scope     Scope object to which the function will be added.
 * @return A new Javascript function object
 */
Ref<JSFunction> addNative1 (const std::string& szName, 
                            const std::string& p1, 
                           JSNativeFn pFn, 
                           Ref<IScope> scope)
{
    Ref<JSFunction> function = JSFunction::createNative(szName, pFn);

    function->addParam(p1);
    scope->set(szName, function);
    
    return function;
}


/**
 * Adds a native function with two arguments
 * @param szName    Function name
 * @param p1        First parameter name
 * @param p1        Second parameter name
 * @param pFn       Pointer to native function
 * @param scope     Scope object to which the function will be added.
 * @return A new Javascript function object
 */
Ref<JSFunction> addNative2 (const std::string& szName, 
                            const std::string& p1, 
                            const std::string& p2, 
                           JSNativeFn pFn, 
                           Ref<IScope> scope)
{
    Ref<JSFunction> function = JSFunction::createNative(szName, pFn);

    function->addParam(p1);
    function->addParam(p2);
    scope->set(szName, function);
    
    return function;
}
