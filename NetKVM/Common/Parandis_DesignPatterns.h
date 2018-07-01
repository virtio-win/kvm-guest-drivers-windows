#pragma once
/*
* Contains various implementations of generic design patterns
*
* Copyright (c) 2018 Red Hat, Inc.
*
* Authors:
*  Sameeh Jubran <sjubran@redhat.com>
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met :
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and / or other materials provided with the distribution.
* 3. Neither the names of the copyright holders nor the names of their contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*/

#include "ParaNdis-Util.h"

template <typename TNotifyObject>
class CObserver {
public:public:
    virtual ~CObserver() {}
    virtual void Notify(TNotifyObject message) { UNREFERENCED_PARAMETER(message); }
protected:
    CObserver() {}

private:
    DECLARE_CNDISLIST_ENTRY(CObserver);

    CObserver(const CObserver& observer);
    CObserver& operator=(const CObserver& observer);
};

template <typename TNotifyObject>
class CObservee{
public:public:
    virtual ~CObservee()
    {
        m_ObserversList.ForEachDetached([](CObserver<TNotifyObject> *observer) { UNREFERENCED_PARAMETER(observer); });
    }
    ULONG  Add(CObserver<TNotifyObject>* observer)
    {
        return m_ObserversList.PushBack(observer);
    }
    void Remove(CObserver<TNotifyObject>* observer)
    {
        m_ObserversList.Remove(observer);
    }
    void NotifyAll(TNotifyObject message)
    {
        m_ObserversList.ForEach([message](CObserver<TNotifyObject> *observer) { observer->Notify(message); });
    }

protected:
    CObservee() {}

private:
    CNdisList<CObserver<TNotifyObject>, CRawAccess, CCountingObject> m_ObserversList;

    CObservee(const CObservee& observee);
    CObservee& operator=(const CObservee& observee);
};
