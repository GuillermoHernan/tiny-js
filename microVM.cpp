/* 
 * File:   microVM.cpp
 * Author: ghernan
 * 
 * Minimalist Virtual Machine for Javascript
 * 
 * A virtual machine with a very small instruction set.
 * 
 * Created on December 2, 2016, 9:18 PM
 */

#include "ascript_pch.hpp"
#include "microVM.h"
#include "ScriptException.h"
#include "asObjects.h"

#include <vector>

using namespace std;


//Forward declarations
////////////////////////////////////////

typedef void (*OpFunction) (const int opCode, ExecutionContext* ec);

int execBlock (const MvmBlock& block, ExecutionContext* ec);
void execInstruction16 (const int opCode, ExecutionContext* ec);
void execInstruction8 (const int opCode, ExecutionContext* ec);
void execPushC8 (const int opCode, ExecutionContext* ec);
void execPushC16 (const int opCode, ExecutionContext* ec);
void execCall8 (const int opCode, ExecutionContext* ec);
void execCall16 (const int opCode, ExecutionContext* ec);
void mvmExecCall (int nArgs, ExecutionContext* ec);
//void callLog (Ref<FunctionScope> fnScope, ExecutionContext* ec);
//void returnLog (Ref<FunctionScope> fnScope, ASValue result, ExecutionContext* ec);
void execCp8 (const int opCode, ExecutionContext* ec);
void execWr8 (const int opCode, ExecutionContext* ec);
void execCp16 (const int opCode, ExecutionContext* ec);
void execWr16 (const int opCode, ExecutionContext* ec);
void execSwap (const int opCode, ExecutionContext* ec);
void execPop (const int opCode, ExecutionContext* ec);
void execPushScope (const int opCode, ExecutionContext* ec);
void execRdLocal (const int opCode, ExecutionContext* ec);
void execWrLocal (const int opCode, ExecutionContext* ec);
void execRdGlobal (const int opCode, ExecutionContext* ec);
void execWrGlobal (const int opCode, ExecutionContext* ec);
void execRdField (const int opCode, ExecutionContext* ec);
void execWrField (const int opCode, ExecutionContext* ec);
void execRdIndex (const int opCode, ExecutionContext* ec);
void execWrIndex (const int opCode, ExecutionContext* ec);
void execNewVar (const int opCode, ExecutionContext* ec);
void execNewConst (const int opCode, ExecutionContext* ec);
void execNewConstField (const int opCode, ExecutionContext* ec);
void execRdParam (const int opCode, ExecutionContext* ec);
void execWrParam (const int opCode, ExecutionContext* ec);
void execNumParams (const int opCode, ExecutionContext* ec);
void execPushThis (const int opCode, ExecutionContext* ec);
void execWrThisP (const int opCode, ExecutionContext* ec);
void execNop (const int opCode, ExecutionContext* ec);

void invalidOp (const int opCode, ExecutionContext* ec);

ASValue getFunction (ASValue inValue, ASValue* thisPtr);

// 8 bit instruction dispatch table.
///////////////////////////////////////
static const OpFunction s_instructions[64] = 
{
    //0
    execCall8,      execCall8,      execCall8,      execCall8, 
    execCall8,      execCall8,      execCall8,      execCall8, 
    
    //8
    execCp8,        execCp8,        execCp8,        execCp8,
    execCp8,        execCp8,        execCp8,        execCp8,
    
    //16
    execWr8,        execWr8,        execWr8,        execWr8,
    execWr8,        execWr8,        execWr8,        execWr8,
    
    //24
    execSwap,       execPop,        execRdField,    execWrField,
    execRdIndex,    execWrIndex,    execNewConstField, invalidOp,
    
    //32
    execRdParam,    execWrParam,    execNumParams,  execPushThis,
    execWrThisP,    invalidOp,      invalidOp,      invalidOp,
    
    //40
    invalidOp,      invalidOp,      invalidOp,      invalidOp,
    invalidOp,      invalidOp,      invalidOp,      invalidOp,
    
    //48
    invalidOp,      invalidOp,      invalidOp,      invalidOp,
    invalidOp,      invalidOp,      invalidOp,      invalidOp,
    
    //56
    invalidOp,      invalidOp,      invalidOp,      invalidOp,
    invalidOp,      invalidOp,      invalidOp,      execNop
};

/**
 * Executes a Micro VM routine
 *
 * @param code
 * @param ec        Execution context
 * @return 
 */
ASValue mvmExecRoutine (Ref<MvmRoutine> code, ExecutionContext* ec, int nParams)
{
    if (code->blocks.empty())
        return jsNull();
    
    int nextBlock = 0;
    
    //Create stack frame
    const size_t stackSize = ec->frames.size();
    CallFrame   frame (&code->constants, 
                       ec->stack.size()-nParams, 
                       nParams,
                       ec->getThisParam());
    ec->frames.push_back(frame);
    
    while (nextBlock >= 0)
    {
        try
        {
            nextBlock = execBlock (code->blocks[nextBlock], ec);
        }
        catch (const RuntimeError& e)
        {
            if (e.Position.Block < 0)
            {
                VmPosition pos (code, nextBlock, e.Position.Instruction);
                throw RuntimeError (e.what(), pos);
            }
            else
                throw e;
        }
    }
    
    //Scope stack unwind.
    ec->frames.pop_back();
    ASSERT (ec->frames.size() == stackSize);
    
    ASSERT (!ec->stack.empty());
    return ec->pop();
}

/**
 * Executes a block of code.
 * @param block
 * @param ec
 * @return Returns the next block to be executed.
 */
int execBlock (const MvmBlock& block, ExecutionContext* ec)
{
    for (size_t i = 0; i < block.instructions.size();)
    {
        const int instructionIndex = i;
        
        try
        {
            int opCode = block.instructions[i++];

            if (opCode & OC_EXT_FLAG)
            {
                if (i >= block.instructions.size())
                    rtError("Unexpected end of instruction");
                opCode = (opCode << 8) | block.instructions[i++];
                execInstruction16 (opCode, ec);
            }
            else
                execInstruction8 (opCode, ec);
        }
        catch (const RuntimeError& e)
        {
            if (e.Position.Instruction < 0)
            {
                VmPosition  pos (Ref<RefCountObj>(), -1, instructionIndex);
                throw RuntimeError(e.what(), pos);
            }
            else 
                throw e;
        }
    }
    
    auto result = ec->pop();
    int next = -1;
    
    if (block.nextBlocks[0] == block.nextBlocks[1])
        next = block.nextBlocks[0];
    else
    {
        const bool r = result.toBoolean(ec);
        
        next = block.nextBlocks[r?1:0];
    }
    
    if (next < 0)
        ec->push(result);
    
    return next;
}

/**
 * Executes a 16 bit instruction
 * @param opCode
 * @param ec
 */
void execInstruction16 (const int opCode, ExecutionContext* ec)
{
    const int decoded = opCode & 0x3FFF;

    if (ec->trace != NULL)
        ec->trace (opCode, ec);
    
    if (decoded >= OC16_PUSHC)
        execPushC16 (decoded, ec);
    else if (decoded <= OC16_CALL_MAX)
        execCall16(decoded, ec);
    else if (decoded <= OC16_CP_MAX)
        execCp16(decoded, ec);
    else if (decoded <= OC16_WR_MAX)
        execWr16(decoded, ec);
    else
        rtError ("Invalid 16 bit opCode: %04X", opCode);
    
}

/**
 * Executes an 8 bit instruction.
 * @param opCode
 * @param ec
 */
void execInstruction8 (const int opCode, ExecutionContext* ec)
{
    if (ec->trace != NULL)
        ec->trace (opCode, ec);

    if (opCode >= OC_PUSHC)
        execPushC8 (opCode, ec);
    else
    {
        //The remaining op codes are decoded with a table (there are only 64)
        s_instructions[opCode](opCode, ec);
    }
}

/**
 * Pushes a constant from the constant table on the top of the stack.
 * Constant index is encoded into the instruction. 8 bit version encodes
 * indexes [0-63]
 * 
 * @param opCode
 * @param ec
 */
void execPushC8 (const int opCode, ExecutionContext* ec)
{
    ec->push(ec->getConstant(opCode - OC_PUSHC));
}

/**
 * Pushes a constant from the constant table on the top of the stack.
 * Constant index is encoded into the instruction. 16 bit version encodes
 * indexes [64 - 8255]
 * 
 * @param opCode
 * @param ec
 */
void execPushC16 (const int opCode, ExecutionContext* ec)
{
    ec->push(ec->getConstant(opCode - (OC16_PUSHC - 64)));
}

/**
 * Executes a function call instruction, 8 bit version
 * @param opCode
 * @param ec
 */
void execCall8 (const int opCode, ExecutionContext* ec)
{
    ASSERT (opCode >= OC_CALL && opCode <= OC_CALL_MAX);
    mvmExecCall (opCode - OC_CALL, ec);
}

/**
 * Executes a function call instruction, 16 bit version
 * @param opCode
 * @param ec
 */
void execCall16 (const int opCode, ExecutionContext* ec)
{
    ASSERT (opCode >= OC16_CALL && opCode <= OC16_CALL_MAX);
    const int   nArgs = (OC_CALL_MAX - OC_CALL) + 1 + (opCode - OC16_CALL);
    
    mvmExecCall(nArgs, ec);
}

/**
 * Executes call instruction.
 * @param nArgs     Argument count
 * @param ec
 */
void mvmExecCall (int nArgs, ExecutionContext* ec)
{
    if (nArgs + 1 > (int)ec->stack.size())
        rtError ("Stack underflow executing function call");
    
    ASValue     thisPtr = jsNull();
    ASValue     fnVal = ec->pop();
    ASValue     result = jsNull();
    
    //Find function value.
    fnVal = getFunction(fnVal, &thisPtr);
    const size_t            initialStack = ec->stack.size() - nArgs;
    
    if (!thisPtr.isNull())
        ec->setThisParam(thisPtr);
    
    if (!fnVal.isNull())
    {
        Ref<JSFunction>     function;
        
        if (fnVal.getType() == VT_FUNCTION)
            function = fnVal.staticCast<JSFunction>();
        else 
        {
            ASSERT (fnVal.getType() == VT_CLOSURE);
            auto closure = fnVal.staticCast<JSClosure>();
            function = closure->getFunction();
            ec->push( closure->value() );
            ++nArgs;
        }
        
        //callLog (fnScope, ec);
        
        if (function->isNative())
        {
            ec->frames.push_back(CallFrame(NULL, 
                                           ec->stack.size()-nArgs, 
                                           nArgs,
                                           ec->getThisParam()));
            result = function->nativePtr()(ec);
            ec->frames.pop_back();
        }
        else
        {
            auto code = function->getCodeMVM().staticCast<MvmRoutine>();
            result = mvmExecRoutine(code, ec, nArgs);
        }
    }
    else
        ec->getThisParam();     //Discard 'this' parameter if no function is going to be called.
    
    //Remove function parameters from the stack
    ec->stack.resize(ec->stack.size() - nArgs);
    ASSERT (initialStack == ec->stack.size());

    //Push result on the stack
    ec->push(result);

    //returnLog(fnScope, result, ec);
}

//static const char *CALL_LOG_DEPTH = "@callLogDepth";

/**
 * Logs function calls, if enabled.
 * @param fnScope
 */
//void callLog (Ref<FunctionScope> fnScope, ExecutionContext* ec)
//{
//    const auto globals = getGlobals();
//    
//    if (!globals->isDefined("callLogger"))
//        return;
//    
//    const auto logFunction = globals->get("callLogger");
//    
//    if (logFunction->isNull())
//        return;
//    
//    //Re-entry guard
//    if (logFunction == fnScope->getFunction())
//        return;
//    
//    ASValue depth;
//    
//    if (!globals->isDefined(CALL_LOG_DEPTH))
//    {
//        depth = jsInt(1);
//        globals->newVar(CALL_LOG_DEPTH, depth, false);
//    }
//    else
//    {
//        depth = globals->get(CALL_LOG_DEPTH);
//        const int iDepth = max (1, toInt32( depth ) + 1);
//        depth = jsInt(iDepth);
//        globals->set(CALL_LOG_DEPTH, depth);
//    }
//    
//    auto obj = JSObject::create();
//    obj->writeField("level", depth, false);
//    obj->writeField("name", jsString(fnScope->getFunction()->getName()), false);
//    obj->writeField("params", fnScope->get("arguments"), false);
//    obj->writeField("this", fnScope->getThis(), false);
//    
//    ec->push(jsNull());     //this
//    ec->push(obj);          //Log entry
//    ec->push(logFunction);
//    
//    mvmExecCall(2, ec);  
//    ec->pop();              //Discard result.
//}

/**
 * Logs function return, if enabled.
 * @param fnScope
 * @param result
 * @param ec
 */
//void returnLog (Ref<FunctionScope> fnScope, ASValue result, ExecutionContext* ec)
//{
//    const auto globals = getGlobals();
//
//    if (!globals->isDefined("callLogger"))
//        return;
//
//    const auto logFunction = globals->get("callLogger");
//    
//    if (!logFunction->isFunction())
//        return;
//    
//    //Re-entry guard
//    if (logFunction == fnScope->getFunction())
//        return;
//    
//    if (!globals->isDefined(CALL_LOG_DEPTH))
//        return;
//    
//    auto depth = globals->get(CALL_LOG_DEPTH);
//    
//    if (depth->getType() != VT_NUMBER)
//        depth = jsInt(0);
//    
//    if (toInt32 (depth) <= 0)
//        globals->set("callLogger", jsNull());    //Remove call logger
//    else
//    {
//        globals->set(CALL_LOG_DEPTH, jsInt(toInt32( depth )-1));
//    
//        auto obj = JSObject::create();
//        obj->writeField("level", depth, false);
//        obj->writeField("name", jsString(fnScope->getFunction()->getName()), false);
//        obj->writeField("result", result, false);
//
//        ec->push(jsNull());     //this
//        ec->push(obj);          //log entry
//        ec->push(logFunction);
//
//        mvmExecCall(2, ec);    
//        ec->pop();              //Discard result.
//    }
//}


/**
 * Copies an element in the stack to the top of the stack
 * @param opCode
 * @param ec
 */
void execCp8 (const int opCode, ExecutionContext* ec)
{
    const size_t offset = opCode - OC_CP;
    
    if (offset+1 > ec->stack.size() )
    {
        rtError ("Stack underflow in copy(CP) operation. Offset: %d Stack: %d", 
                 (int)offset, (int)ec->stack.size());
    }
    
    ec->push (*(ec->stack.rbegin() + offset));
}

/**
 * Writes the current top element of the stack into a position 
 * deeper on the stack, overwriting that value.
 * Offset 0 is the element just under the top element.
 * The top element is not removed, remains at the top of the stack
 * @param opCode
 * @param ec
 */
void execWr8 (const int opCode, ExecutionContext* ec)
{
    const size_t offset = (opCode - OC_WR)+1;
    
    if (offset + 1 > ec->stack.size() )
    {
        rtError ("Stack underflow in write(WR) operation. Offset: %d Stack: %d", 
                 (int)offset, (int)ec->stack.size());
    }
    
    *(ec->stack.rbegin() + offset) = ec->stack.back();
}

/**
 * Copies an element in the stack to the top of the stack
 * @param opCode
 * @param ec
 */
void execCp16 (const int opCode, ExecutionContext* ec)
{
    const size_t offset = (opCode - OC16_CP) + (OC_CP_MAX - OC_CP) + 1;
    
    if (offset+1 > ec->stack.size() )
    {
        rtError ("Stack underflow in copy(CP) operation. Offset: %d Stack: %d", 
                 (int)offset, (int)ec->stack.size());
    }
    
    ec->push (*(ec->stack.rbegin() + offset));
}

/**
 * Writes the current top element of the stack into a position 
 * deeper on the stack, overwriting that value.
 * Offset 0 is the element just under the top element.
 * The top element is not removed, remains at the top of the stack
 * @param opCode
 * @param ec
 */
void execWr16 (const int opCode, ExecutionContext* ec)
{
    const size_t offset = (opCode - OC16_WR) + (OC_WR_MAX - OC_WR) + 2;
    
    if (offset + 1 > ec->stack.size() )
    {
        rtError ("Stack underflow in write(WR) operation. Offset: %d Stack: %d", 
                 (int)offset, (int)ec->stack.size());
    }
    
    *(ec->stack.rbegin() + offset) = ec->stack.back();
}

/**
 * Exchanges the top two elements of the stack
 * @param opCode
 * @param ec
 */
void execSwap (const int opCode, ExecutionContext* ec)
{
    const ASValue  a = ec->pop();
    const ASValue  b = ec->pop();
    
    ec->push(a);
    ec->push(b);
}

/**
 * Discards the top element of the stack
 * @param opCode
 * @param ec
 */
void execPop (const int opCode, ExecutionContext* ec)
{
    ec->pop();
}

/**
 * Reads an object field.
 * @param opCode
 * @param ec
 */
void execRdField (const int opCode, ExecutionContext* ec)
{
    const ASValue  name = ec->pop();
    const ASValue  objVal = ec->pop();
    const ASValue  val = objVal.readField(name.toString(ec));
    
    ec->push(val);
}

/**
 * Writes a value to an object field.
 * @param opCode
 * @param ec
 */
void execWrField (const int opCode, ExecutionContext* ec)
{
    const ASValue  val = ec->pop();
    const ASValue  name = ec->pop();
    ASValue  objVal = ec->pop();
    
    objVal.writeField (name.toString(ec), val, false);
    ec->push(val);
}

/**
 * Executes an 'indexed read' operation.
 * @param opCode
 * @param ec
 */
void execRdIndex (const int opCode, ExecutionContext* ec)
{
    const ASValue  key = ec->pop();
    const ASValue  container = ec->pop();
    const ASValue  val = container.getAt(key, ec);
    
    ec->push(val);
}

/**
 * Executes an 'indexed write operation
 * @param opCode
 * @param ec
 */
void execWrIndex (const int opCode, ExecutionContext* ec)
{
    const ASValue  val = ec->pop();
    const ASValue  key = ec->pop();
    const ASValue  container = ec->pop();
    
    container.setAt(key, val, ec); 
    ec->push(val);
}

/**
 * Creates a new constant field in an object. Very similar to 'WR_FIELD', but the
 * written field won't be modifiable.
 * @param opCode
 * @param ec
 */
void execNewConstField (const int opCode, ExecutionContext* ec)
{
    const ASValue  val = ec->pop();
    const ASValue  name = ec->pop();
    ASValue  objVal = ec->pop();
    
    objVal.writeField (name.toString(ec), val, true);
    ec->push(val);
}

/**
 * Reads a function call parameter.
 * Pops the parameter index from the top of the stack, and pushes the
 * parameter value.
 * If the index is out of range or not an integer, it pushes a 'null' value
 * on the top of the stack.
 * @param opCode
 * @param ec
 */
void execRdParam (const int opCode, ExecutionContext* ec)
{
    const auto indexVal = ec->pop();
    ASValue    result = jsNull();
    
    if ( indexVal.isInteger() )
    {
        const int           index = indexVal.toInt32();
        const CallFrame&    curFrame = ec->frames.back();
        
        if (index >= 0 && index < (int)curFrame.numParams)
            result = ec->stack[curFrame.paramsIndex + index];
    }
    
    ec->push(result);
}

/**
 * Overwrites a function call parameter.
 * Pops the parameter index and the new value from the top of the stack, 
 * and pushes back the value
 * If the index is out of range or not an integer, it pushes a 'null' value
 * on the top of the stack, instead of the value.
 * @param opCode
 * @param ec
 */
void execWrParam (const int opCode, ExecutionContext* ec)
{
    auto        value = ec->pop();
    const auto  paramIndex = ec->pop();

    if ( paramIndex.isInteger() )
    {
        const int           index = paramIndex.toInt32();
        const CallFrame&    curFrame = ec->frames.back();
        
        if (index >= 0 && index < (int)curFrame.numParams)
            ec->stack[curFrame.paramsIndex + index] = value;
        else
            value = jsNull();
    }
    else
        value = jsNull();
    
    ec->push(value);
}

/**
 * Places on the top of the stack the number of parameters passed to the 
 * actual function being executed
 * @param opCode
 * @param ec
 */
void execNumParams (const int opCode, ExecutionContext* ec)
{
    const CallFrame&    curFrame = ec->frames.back();

    ec->push(jsSizeT(curFrame.numParams));
}

/**
 * Pushes the value of the current "this" register on top of the stack.
 * @param opCode
 * @param ec
 */
void execPushThis (const int opCode, ExecutionContext* ec)
{
    ec->push(ec->getThis());
}

/**
 * Writes the value on top of the stack in "this parameter" register.
 * This register is not the register readable with 'OC_PUSH_THIS', it
 * will become the value of that register on the next call instruction.
 * 
 * The value is not removed from the top of the stack, so this instruction
 * leaves the stack unchanged.
 * @param opCode
 * @param ec
 */
void execWrThisP (const int opCode, ExecutionContext* ec)
{
    ec->checkStackNotEmpty();
    
    ec->setThisParam (ec->stack.back());
}

/**
 * No operation. It does nothing
 * @param opCode
 * @param ec
 */
void execNop (const int opCode, ExecutionContext* ec)
{
    //This one is easy
}

void invalidOp (const int opCode, ExecutionContext* ec)
{
    rtError ("Invalid operation code: %04X", opCode);
}

bool ExecutionContext::checkStackNotEmpty()
{
    if (stack.empty())
        rtError ("Stack underflow!");

    return !stack.empty();
}

ASValue ExecutionContext::getConstant (size_t index)const
{
    ASSERT (!frames.empty());
    ASSERT (index < frames.back().constants->size());

    return (*frames.back().constants)[index];
}

ASValue ExecutionContext::getParam (size_t index)const
{
    ASSERT (!frames.empty());
    
    const CallFrame&    curFrame = frames.back();
    if (index < curFrame.numParams)
        return stack[curFrame.paramsIndex + index];
    else
        return jsNull();
}

ASValue ExecutionContext::getThis ()const
{
    ASSERT (!frames.empty());
    const CallFrame&    curFrame = frames.back();

    return curFrame.thisValue;
}

size_t ExecutionContext::getNumParams ()const
{
    ASSERT (!frames.empty());
    
    const CallFrame&    curFrame = frames.back();
    return curFrame.numParams > 0;
}

/**
 * Gets the function to call in place of the input value.
 * @param inValue
 * @param thisPtr
 * @return 
 */
ASValue getFunction (ASValue inValue, ASValue* thisPtr)
{
    ASValue result = inValue;
    
    *thisPtr = jsNull();
    
    while (result.getType() == VT_OBJECT)
    {
        *thisPtr = result;
        result = result.readField("call");
    }
    
    switch (result.getType())
    {
    case VT_CLASS:
        *thisPtr = result;
        return result.staticCast<JSClass>()->getConstructor();
        
    case VT_FUNCTION:
    case VT_CLOSURE:
        return result;
        
    default:
        return jsNull();
    }
}
