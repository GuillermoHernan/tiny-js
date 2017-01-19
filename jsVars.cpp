/* 
 * File:   JsVars.cpp
 * Author: ghernan
 * 
 * Javascript variables and values implementation code
 *
 * Created on November 21, 2016, 7:24 PM
 */

#include "jsVars.h"
#include "OS_support.h"
#include "utils.h"
#include "executionScope.h"
#include "asString.h"
#include "microVM.h"

#include <cstdlib>
#include <math.h>

using namespace std;

Ref<JSValue> JSValue::call (Ref<FunctionScope> scope)
{
    error ("Not a callable object: %s", toString().c_str());
    return undefined();
}

Ref<JSValue> JSValue::readFieldStr(const std::string& strKey)const
{
    return readField(jsString(strKey));
}

Ref<JSValue> JSValue::writeFieldStr(const std::string& strKey, Ref<JSValue> value)
{
    return writeField(jsString(strKey), value);
}

Ref<JSValue> JSValue::newConstFieldStr(const std::string& strKey, Ref<JSValue> value)
{
    return newConstField(jsString(strKey), value);
}

/**
 * Gives an string representation of the type name
 * @return 
 */
std::string getTypeName(JSValueTypes vType)
{
    typedef map<JSValueTypes, string> TypesMap;
    static TypesMap types;

    if (types.empty())
    {
        types[VT_UNDEFINED] = "undefined";
        types[VT_NULL] = "null";
        types[VT_NUMBER] = "Number";
        types[VT_BOOL] = "Boolean";
        types[VT_ACTOR_REF] = "Actor reference";
        types[VT_INPUT_EP_REF] = "Input EP reference";
        types[VT_OUTPUT_EP_REF] = "Output EP reference";
        types[VT_CLASS] = "Class";
        types[VT_OBJECT] = "Object";
        types[VT_STRING] = "String";
        types[VT_ARRAY] = "Array";
        types[VT_ACTOR] = "Actor";
        types[VT_FUNCTION] = "Function";
        types[VT_ACTOR_CLASS] = "Actor class";
        types[VT_INPUT_EP] = "Input EP";
        types[VT_OUTPUT_EP] = "Output EP";
    }

    ASSERT(types.find(vType) != types.end());
    return types[vType];
}

/**
 * Class for 'undefined' values.
 */
class JSUndefined : public JSValueBase<VT_UNDEFINED>
{
public:

    virtual std::string toString()const
    {
        return "undefined";
    }
};

/**
 * Class for 'null' values.
 */
class JSNull : public JSValueBase<VT_NULL>
{
public:

    virtual std::string toString()const
    {
        return "null";
    }
};

/**
 * Gets the 'undefined value.
 * @return 
 */
Ref<JSValue> undefined()
{
    static Ref<JSValue> value = refFromNew(new JSUndefined);
    return value;
}

Ref<JSValue> jsNull()
{
    static Ref<JSValue> value = refFromNew(new JSNull);
    return value;
}

Ref<JSBool> jsTrue()
{
    static Ref<JSBool> value = refFromNew(new JSBool(true));
    return value;
}

Ref<JSBool> jsFalse()
{
    static Ref<JSBool> value = refFromNew(new JSBool(false));
    return value;
}

Ref<JSValue> jsBool(bool value)
{
    if (value)
        return jsTrue();
    else
        return jsFalse();
}

Ref<JSValue> jsInt(int value)
{
    return JSNumber::create(value);
}

Ref<JSValue> jsDouble(double value)
{
    return JSNumber::create(value);
}

Ref<JSValue> jsString(const std::string& value)
{
    return JSString::create(value);
}

/**
 * Transforms a 'JSValue' into a string which can be used to search the store.
 * @param key
 * @return 
 */
std::string key2Str(Ref<JSValue> key)
{
    if (!key->isPrimitive())
        error("Invalid array index: %s", key->toString().c_str());
    else if (key->getType() == VT_NUMBER)
        return double_to_string(key->toDouble());
    else
        return key->toString();

    return "";
}

/**
 * Class for numeric constants. 
 * It also stores the original string representation, to have an accurate string 
 * representation
 */
class JSNumberConstant : public JSNumber
{
public:

    static Ref<JSNumberConstant> create(const std::string& text)
    {
        if (text.size() > 0 && text[0] == '0' && isOctal(text))
        {
            const unsigned value = strtoul(text.c_str() + 1, NULL, 8);

            return refFromNew(new JSNumberConstant(value, text));
        }
        else
        {
            const double value = strtod(text.c_str(), NULL);

            return refFromNew(new JSNumberConstant(value, text));
        }
    }

    virtual std::string toString()const
    {
        return m_text;
    }

private:

    JSNumberConstant(double value, const std::string& text)
    : JSNumber(value), m_text(text)
    {
    }

    string m_text;
};

Ref<JSValue> createConstant(CScriptToken token)
{
    if (token.type() == LEX_STR)
        return JSString::create(token.strValue());
    else
        return JSNumberConstant::create(token.text());
}

/**
 * Gets a member form a scope, and ensures it is an object.
 * If not an object, returns NULL
 * @param pScope
 * @param name
 * @return 
 */
Ref<JSObject> getObject(Ref<IScope> pScope, const std::string& name)
{
    Ref<JSValue> value = pScope->get(name);

    if (!value.isNull())
    {
        if (value->isObject())
            return value.staticCast<JSObject>();
    }

    return Ref<JSObject>();
}

/**
 * Transforms a NULL pointer into an undefined value. If not undefined,
 * just returns the input value.
 * @param value input value to check
 * @return 
 */
Ref<JSValue> null2undef(Ref<JSValue> value)
{
    if (value.isNull())
        return undefined();
    else
        return value;
}

/**
 * Checks if the object has any kind of 'null' states: internal NULL pointer,
 * Javascript null, Javascript undefined.
 * @param value
 * @return 
 */
bool nullCheck(Ref<JSValue> value)
{
    if (value.isNull())
        return true;
    else
        return value->isNull();
}

/**
 * Compares two javascript values.
 * @param a
 * @param b
 * @return 
 */
double jsValuesCompare(Ref<JSValue> a, Ref<JSValue> b)
{
    auto typeA = a->getType();
    auto typeB = b->getType();

    if (typeA != typeB)
        return typeA - typeB;
    else
    {
        if (typeA <= VT_NULL)
            return 0;
        else if (typeA <= VT_BOOL)
            return a->toDouble() - b->toDouble();
        else if (typeA == VT_STRING)
            return a->toString().compare(b->toString());
        else
            return a.getPointer() - b.getPointer();
    }
}

/**
 * Converts a 'JSValue' into a 32 bit signed integer.
 * If the conversion is not posible, it returns zero. Therefore, the use
 * of 'isInteger' function is advised.
 * @param a
 * @return 
 */
int toInt32(Ref<JSValue> a)
{
    const double v = a->toDouble();

    if (isnan(v))
        return 0;
    else
        return (int) v;
}

/**
 * Converts a value into a 64 bit integer.
 * @param a
 * @return The integer value. In case of failure, it returns the largest 
 * number representable by a 64 bit integer (0xFFFFFFFFFFFFFFFF), which cannot
 * be represented by a double.
 */
unsigned long long toUint64(Ref<JSValue> a)
{
    const double v = a->toDouble();

    if (isnan(v))
        return 0xFFFFFFFFFFFFFFFF;
    else
        return (unsigned long long) v;
}

size_t toSizeT(Ref<JSValue> a)
{
    return (size_t) toUint64(a);
}

/**
 * Checks if a value is an integer number
 * @param a
 * @return 
 */
bool isInteger(Ref<JSValue> a)
{
    const double v = a->toDouble();

    return floor(v) == v;
}

/**
 * Checks if a value is a unsigned integer number.
 * @param a
 * @return 
 */
bool isUint(Ref<JSValue> a)
{
    const double v = a->toDouble();

    if (v < 0)
        return false;
    else
        return floor(v) == v;
}

/**
 * Creates a 'deep frozen' copy of an object, making deep frozen copies of 
 * all descendants which are necessary,
 * @param obj
 * @param transformed
 * @return 
 */
Ref<JSValue> deepFreeze(Ref<JSValue> obj, JSValuesMap& transformed)
{
    if (obj.isNull())
        return obj;

    if (obj->getMutability() == MT_DEEPFROZEN)
        return obj;

    auto it = transformed.find(obj);
    if (it != transformed.end())
        return it->second;

    ASSERT(obj->isObject());

    //Clone object
    auto newObject = obj->unFreeze(true).staticCast<JSObject>();
    transformed[obj] = newObject;

    auto object = obj.staticCast<JSObject>();
    auto keys = object->getKeys();

    for (size_t i = 0; i < keys.size(); ++i)
    {
        auto key = keys[i];
        auto value = deepFreeze(object->readField(key), transformed);
        newObject->writeField(key, value);
    }

    newObject->m_mutability = MT_DEEPFROZEN;

    return newObject;
}

Ref<JSValue> deepFreeze(Ref<JSValue> obj)
{
    JSValuesMap transformed;
    return deepFreeze(obj, transformed);
}

/**
 * Writes to a variable in a variable map. If the variable already exist, and
 * is a constant, it throw an exception.
 * @param map
 * @param name
 * @param value
 * @param isConst   If true, it creates a new constant
 */
void checkedVarWrite(VarMap& map, const std::string& name, Ref<JSValue> value, bool isConst)
{
    auto it = map.find(name);

    if (it != map.end() && it->second.isConst())
        error("Trying to write to constant '%s'", name.c_str());

    map[name] = VarProperties(value, isConst);
}

/**
 * Deletes a variable form a variable map. Throws exceptions if the variable 
 * does not exist or if it is a constant.
 * @param map
 * @param name
 * @return 
 */
Ref<JSValue> checkedVarDelete(VarMap& map, const std::string& name)
{
    auto it = map.find(name);
    Ref<JSValue> value;

    if (it == map.end())
        error("'%s' is not defined", name.c_str());
    else if (it->second.isConst())
        error("Trying to delete constant '%s'", name.c_str());
    else
    {
        value = it->second.value();
        map.erase(it);
    }

    return value;
}


// JSNumber
//
//////////////////////////////////////////////////

/**
 * Construction function
 * @param value
 * @return 
 */
Ref<JSNumber> JSNumber::create(double value)
{
    return refFromNew(new JSNumber(value));
}

std::string JSNumber::toString()const
{
    //TODO: Review the standard. Find about number to string conversion formats.
    return double_to_string(m_value);
}



// JSFunction
//
//////////////////////////////////////////////////

/**
 * Creates a Javascript function 
 * @param name Function name
 * @return A new function object
 */
Ref<JSFunction> JSFunction::createJS(const std::string& name,
                                     const StringVector& params,
                                     Ref<RefCountObj> code)
{
    return refFromNew(new JSFunction(name, params, code));
}

/**
 * Creates an object which represents a native function
 * @param name  Function name
 * @param fnPtr Pointer to the native function.
 * @return A new function object
 */
Ref<JSFunction> JSFunction::createNative(const std::string& name,
                                         const StringVector& params,
                                         JSNativeFn fnPtr)
{
    return refFromNew(new JSFunction(name, params, fnPtr));
}

JSFunction::JSFunction(const std::string& name,
                       const StringVector& params,
                       JSNativeFn pNative) :
m_name(name),
m_params(params),
m_pNative(pNative)
{
}

JSFunction::JSFunction(const std::string& name,
                       const StringVector& params,
                       Ref<RefCountObj> code) :
m_name(name),
m_params(params),
m_codeMVM(code),
m_pNative(NULL)
{
}

JSFunction::~JSFunction()
{
    //    printf ("Destroying function: %s\n", m_name.c_str());
}

/**
 * Executes a function call (invoked by MicroVM)
 * @param scope
 * @param ec
 * @return 
 */
Ref<JSValue> JSFunction::call (Ref<FunctionScope> scope)
{
    if (isNative())
        return nativePtr()(scope.getPointer());
    else
    {
        auto    code = getCodeMVM().staticCast<MvmRoutine>();
        
        return mvmExecute(code, scope->getGlobals(), scope);
    }
}

/**
 * String representation of the function.
 * Just the function and the parameter list, no code.
 * @return 
 */
std::string JSFunction::toString()const
{
    ostringstream output;

    //TODO: another time a code very like to 'String.Join'. But sadly, each time
    //read from a different container.

    output << "function " << getName() << " (";

    for (size_t i = 0; i < m_params.size(); ++i)
    {
        if (i > 0)
            output << ',';

        output << m_params[i];
    }

    output << ")";
    return output.str();
}
