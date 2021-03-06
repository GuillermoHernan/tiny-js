/*
 * This is a program to run all the tests in the tests folder...
 */

#include "ascript_pch.hpp"
#include "utils.h"
#include "scriptMain.h"
#include "mvmCodegen.h"
#include "jsParser.h"
#include "semanticCheck.h"
#include "TinyJS_Functions.h"
//#include "actorRuntime.h"
#include "jsArray.h"
#include "ScriptException.h"
#include "microVM.h"

#include <assert.h>
#include <sys/stat.h>
#include <string>
#include <sstream>
#include <stdio.h>

using namespace std;

/**
 * Generic JSON format logger.
 * It is used to generate call log.
 */
class JsonLogger
{
public:
    JsonLogger (const string& filePath) : m_path (filePath)
    {
        FILE*   pf = fopen (m_path.c_str(), "w");
        if (pf)
        {
            fclose(pf);
            log ("[", false);
            m_first = true;
        }
    }
    
    ~JsonLogger()
    {
        log ("]", false);
    }
    
    void log (const string& text, bool comma = true)
    {
        FILE*   pf = fopen (m_path.c_str(), "a+");
        
        if (pf)
        {
            if (comma && !m_first)
                fprintf (pf, ",%s\n", text.c_str());
            else
                fprintf (pf, "%s\n", text.c_str());
            m_first = false;
            fclose(pf);
        }
    }
    
private:
    string  m_path;
    bool    m_first;
};


JsonLogger*  s_curFunctionLogger = NULL;

static string s_traceLoggerPath;

/**
 * Logs MicroVM instructions
 */
static void traceLogger (int opCode, const ExecutionContext* ec)
{
    FILE *pf = fopen(s_traceLoggerPath.c_str(), "a+");
    
    if (pf != NULL)
    {
        string instruction = mvmDisassemblyInstruction (opCode, *ec->frames.back().constants);
        
        fprintf (pf, "%-24s\t", instruction.c_str());
        if (ec->stack.empty())
            fprintf (pf, "[Empty stack]\n");
        else{
            //Print the top value of the stack, but using only basic string conversion
            ASValue value = ec->stack.back();
            
            if (value.getType() == VT_STRING)
                fprintf (pf, "[\"%s\"]\n", value.toString(NULL).c_str());
            else
                fprintf (pf, "[%s]\n", value.toString(NULL).c_str());
        }
        fclose(pf);
    }
}

static void resetFile (const char* szPath)
{
    FILE *pf = fopen (szPath, "w");
    
    if (pf != NULL)
        fclose(pf);
}

/**
 * Assertion function exported to tests
 * @param pScope
 * @return 
 */
ASValue assertFunction(ExecutionContext* ec)
{
    auto    value =  ec->getParam(0);
    
    if (!value.toBoolean(ec))
    {
        auto    text =  ec->getParam(1).toString(ec);
        
        rtError("Assertion failed: %s", text.c_str());
    }
    
    return jsNull();
}

/**
 * Executes some code using eval, and expects that it throws a 'CScriptException'.
 * It catches the exception, and returns 'true'. If no exception is throw, it 
 * throws an exception to indicate a test failure.
 * @param pScope
 * @return 
 */
ASValue expectError(ExecutionContext* ec)
{
    string  code =  ec->getParam(0).toString(ec);
    
    try
    {
        evaluate (code.c_str(), createDefaultGlobals(), ec->modulePath, ec);
    }
    catch (CScriptException& error)
    {
        return jsTrue();
    }
    
    rtError ("No exception thrown: %s", code.c_str());
    
    return jsFalse();
}


/**
 * Function to write on standard output
 * @param pScope
 * @return 
 */
ASValue printLn(ExecutionContext* ec)
{
    auto    text =  ec->getParam(0);
    
    printf ("%s\n", text.toString(ec).c_str());
    
    return jsNull();
}

/**
 * Script exported function to enable trace log.
 * @param ec
 * @return 
 */
ASValue enableTraceLog(ExecutionContext* ec)
{
    auto enable = ec->getParam(0);
    
    if (enable.isNull() || enable.toBoolean(ec) == true)
        ec->trace = traceLogger;
    else
        ec->trace = NULL;
    
    return jsNull();
}

ASValue enableCallLog(ExecutionContext* ec)
{
    //TODO: Enable again
//    auto logFn = [](ExecutionContext* ec) -> ASValue
//    {
//        auto entry = ec->getParam(0);
//
//        s_curFunctionLogger->log(entry->getJSON(0));
//        return jsNull();
//    };
//    addNative("function callLogger(x)", logFn, getGlobals(), false);
    
    return jsNull();
}



/**
 * Gives access to the parser to the tested code.
 * Useful for tests which target the parser.
 * @param pScope
 * @return 
 */
ASValue asParse(ExecutionContext* ec)
{
    string          code =  ec->getParam(0).toString(ec);
    CScriptToken    token (code.c_str());
    auto            result = JSArray::create();

    //Parsing loop
    token = token.next();
    while (!token.eof())
    {
        const ParseResult   parseRes = parseStatement (token);

        result->push(parseRes.ast->toJS());
        token = parseRes.nextToken;
    }
    
    return result->value();
}

/**
 * Funs a test script loaded from a file.
 * @param szFile        Path to the test script.
 * @param testDir       Directory in which the test script is located.
 * @param resultsDir    Directory in which tests results are written
 * @return 
 */
bool run_test(const std::string& szFile, const string &testDir, const string& resultsDir)
{
    printf("TEST %s ", szFile.c_str());
    
    string script = readTextFile(szFile);
    if (script.empty())
    {
        printf("Cannot read file: '%s'\n", szFile.c_str());
        return false;
    }
    
    const string relPath = szFile.substr (testDir.size());
    const string testName = removeExt( fileFromPath(relPath));
    string testResultsDir = resultsDir + removeExt(relPath) + '/';
    bool pass = false;

    auto globals = createDefaultGlobals();
    
    globals->writeField("result", jsInt(0), false);
    addNative("function assert(value, text)", assertFunction, globals);
    addNative("function printLn(text)", printLn, globals);
    addNative("function expectError(code)", expectError, globals);
    addNative("function asParse(code)", asParse, globals);
    addNative("function enableCallLog()", enableCallLog, globals);
    addNative("function enableTraceLog()", enableTraceLog, globals);
    try
    {
        //This code is copied from 'evaluate', to log the intermediate results 
        //generated from each state
        CScriptToken    token (script.c_str());

        //Script parse
        auto    parseRes = parseScript(token.next());
        auto    ast = parseRes.ast;

        //Write Abstract Syntax Tree
        const string astJSON = ast->toJS().getJSON(0);
        writeTextFile(testResultsDir + testName + ".ast.json", astJSON);
        
        //Semantic analysis
        semanticCheck(ast);

        //Code generation.
        CodeMap                 cMap;
        const Ref<MvmRoutine>   code = scriptCodegen(ast, &cMap);

        //Write disassembly
        writeTextFile(testResultsDir + testName + ".asm.json", mvmDisassembly(code));
        
        //Call logger setup. Not enabled until the script code calls
        //'enableCallLog'
        JsonLogger  callLogger (testResultsDir + testName + ".calls.json");
        s_curFunctionLogger = &callLogger;
        
        //Execution traces log.
        s_traceLoggerPath = testResultsDir + testName + ".trace.log";
        resetFile (s_traceLoggerPath.c_str());

        //Execution
        evaluate (code, &cMap, globals, szFile, NULL);

        auto result = globals->readField("result");
        if (result.toString() != "exception")
            pass = result.toBoolean();
        else
            printf ("No exception thrown\n");
    }
    catch (const CScriptException &e)
    {
        if (globals->readField("result").toString() == "exception")
            pass = true;
        else
            printf("ERROR: %s\n", e.what());
    }

    //Write globals
    writeTextFile(testResultsDir + testName + ".globals.json", globals->getJSON(0));

    if (pass)
        printf("PASS\n");
    else
        printf("FAIL\n");

    return pass;
}

/**
 * Test program entry point.
 * @param argc
 * @param argv
 * @return 
 */
int main(int argc, char **argv)
{
    const string testsDir = "./tests/";
    const string resultsDir = "./tests/results/";
    
    printf("TinyJS test runner\n");
    printf("USAGE:\n");
    printf("   ./run_tests test.js       : run just one test\n");
    printf("   ./run_tests               : run all tests\n");
    if (argc == 2)
    {
        printf("Running test: %s\n", argv[1]);
        
        return !run_test(testsDir + argv[1], testsDir, resultsDir);
    }
    else
        printf("Running all tests!\n");

    int test_num = 1;
    int count = 0;
    int passed = 0;
    
    //TODO: Run all tests in the directory (or even in subdirectories). Do not depend
    //on test numbers.

    while (test_num < 1000)
    {
        char name[32];
        sprintf_s(name, "test%03d.js", test_num);
        
        const string    szPath = testsDir + name;
        // check if the file exists - if not, assume we're at the end of our tests
        FILE *f = fopen(szPath.c_str(), "r");
        if (!f) break;
        fclose(f);

        if (run_test(szPath, testsDir, resultsDir))
            passed++;
        count++;
        test_num++;
    }

    printf("Done. %d tests, %d pass, %d fail\n", count, passed, count - passed);

    return 0;
}
