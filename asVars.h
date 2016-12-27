/* 
 * File:   asVars.h
 * Author: ghernan
 * 
 * Async script basic data types. Contains the data types related to actor system.
 * The rest are in 'jsVars.*'
 *
 * Created on December 25, 2016, 12:53 PM
 */

#ifndef ASVARS_H
#define	ASVARS_H

#pragma once

#include "jsVars.h"

class AsEndPoint;

/**
 * Actor class runtime object
 */
class AsActorClass : public JSObject
{
public:
    static Ref<AsActorClass>    create (const std::string& name)
    {
        return refFromNew (new AsActorClass(name));
    }

    virtual JSValueTypes getType()const
    {
        return VT_ACTOR_CLASS;
    }
    
    Ref<AsEndPoint> getEndPoint (const std::string& name);

    Ref<AsEndPoint> getConstructor ()
    {
        return getEndPoint("@start");
    }
protected:
    AsActorClass (const std::string& name);
};

class AsActorRef;
class AsEndPointRef;
typedef std::vector < Ref<AsActorRef> > AsActorList;

/**
 * Actor runtime object
 */
class AsActor : public JSObject
{
public:
    
    static Ref<AsActor> create (Ref<AsActorClass> cls, 
                                Ref<GlobalScope> globals,
                                Ref<AsActorRef> parent)
    {
        return refFromNew(new AsActor(cls, globals, parent));
    }

    virtual JSValueTypes getType()const
    {
        return VT_ACTOR;
    }
    
    void setOutputConnection (const std::string& msgName, Ref<AsEndPointRef> dst)
    {
        m_outputConections[msgName] = dst;
    }
    
    Ref<AsEndPointRef> getConnectedEp (const std::string& msgName)const;
    
    bool isRunning()const
    {
        return !m_finished;
    }
    
    void forceStop()
    {
        m_finished = true;
    }    
        
    Ref<JSValue> getResult()
    {
        return m_result;
    }
    
    Ref<AsEndPoint> getEndPoint (const std::string& name)
    {
        return m_cls->getEndPoint(name);
    }

    Ref<GlobalScope> getGlobals()const
    {
        return m_globals;
    }
    
    Ref<AsActorRef> getParent()const
    {
        return m_parent;
    }
    
protected:
    AsActor (Ref<AsActorClass> cls, Ref<GlobalScope> globals, Ref<AsActorRef> parent) : 
        JSObject(DefaultPrototype)
        , m_cls (cls)
        , m_globals (globals)
        , m_parent (parent)
        , m_result(undefined())
        , m_finished(false)
    {
    }
    
private:
    const Ref<AsActorClass> m_cls;
    Ref<GlobalScope>    m_globals;
    Ref<AsActorRef>     m_parent;
    
    AsActorList         m_childActors;
    
    typedef std::map<std::string, Ref<AsEndPointRef> > ConnectionMap;
    ConnectionMap       m_outputConections;
    Ref<JSValue>        m_result;
    
    bool                m_finished;
};


/**
 * Actor reference object
 * @param cls
 * @return 
 */
class AsActorRef : public JSObject
{
public:
    static Ref<AsActorRef> create (Ref<AsActor> actor)
    {
        return refFromNew(new AsActorRef(actor));
    }

    virtual JSValueTypes getType()const
    {
        return VT_ACTOR_REF;
    }

    bool isRunning()
    {
        return m_ref->isRunning();
    }
        
    Ref<AsActor> getActor()const
    {
        return m_ref;
    }
    
    Ref<JSValue> getResult()
    {
        if (isRunning())
            return undefined();
        else
            return m_ref->getResult();
    }
    
    Ref<AsEndPointRef> getEndPoint (const std::string& name);
    
protected:
    AsActorRef (Ref<AsActor> actor) : JSObject(DefaultPrototype), m_ref (actor)
    {
    }
    
private:
    const Ref<AsActor> m_ref;
    
};

/**
 * Message runtime object
 */
class AsEndPoint : public JSFunction
{
public:
    static Ref<AsEndPoint> create (const std::string& name, bool input)
    {
        return refFromNew (new AsEndPoint (name, input));
    }
    
    bool isInput()const
    {
        return m_isInput;
    }

    virtual JSValueTypes getType()const
    {
        return isInput() ? VT_INPUT_EP : VT_OUTPUT_EP;
    }
    
protected:
    AsEndPoint (const std::string& name, bool input) :
    JSFunction(name, NULL), m_isInput (input)
    {

    }
    
private:
    const bool m_isInput;
};

//TODO: May be, I am abusing of subclassing from 'JSFunction'. There should
//be something like a 'JSCallable' base class.

/**
 * Async Script message reference.
 * Holds a reference to the message, and to the actor.
 */
class AsEndPointRef : public JSFunction
{
public:
    static Ref<AsEndPointRef> create (Ref<AsEndPoint> endPoint, Ref<AsActorRef> actor)
    {
        return refFromNew (new AsEndPointRef (endPoint, actor));
    }
    
    bool isInput()const
    {
        return m_endPoint->isInput();
    }
    
    virtual JSValueTypes getType()const
    {
        return (m_endPoint->getType() == VT_OUTPUT_EP) ? VT_OUTPUT_EP_REF : VT_INPUT_EP_REF;
    }
    
    Ref<AsActorRef> getActor()const
    {
        return m_actor;
    }
    
    Ref<AsEndPoint> getEndPoint()const
    {
        return m_endPoint;
    }
    
protected:
    AsEndPointRef (Ref<AsEndPoint> endPoint, Ref<AsActorRef> actor) :
    JSFunction("", NULL), m_endPoint (endPoint), m_actor(actor)
    {
    }
    
private:
    const Ref<AsEndPoint>   m_endPoint;
    const Ref<AsActorRef>   m_actor;
};

#endif	/* ASVARS_H */
