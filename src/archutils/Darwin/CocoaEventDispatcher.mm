#include "global.h"
#include "CocoaEventDispatcher.h"

CocoaEventDispatcher CocoaEventDispatcher::sharedDispatcher;

CocoaEventDispatcher::CocoaEventDispatcher()
    : m_Mutex("Cocoa Event Dispatcher")
    , m_NextResponderID(1)
{
}

unsigned CocoaEventDispatcher::AddResponder(const std::function<void(EVENT_TYPE)> &resp)
{
    LockMut(m_Mutex);

    const unsigned respID = m_NextResponderID++;
    m_Responders.push_back(CocoaEventDispatcher::Responder
    {
        respID,
        resp
    });

    return respID;
}

void CocoaEventDispatcher::RemoveResponder(unsigned respID)
{
    LockMut(m_Mutex);
    m_Responders.erase(find_if(m_Responders.begin(), m_Responders.end(), [=](const auto &resp)
    {
        return resp.ID == respID;
    }));
}

void CocoaEventDispatcher::DispatchEvent(EVENT_TYPE e)
{
    LockMut(m_Mutex);

    for (const auto &responder : m_Responders)
    {
        responder.Function(e);
    }
}
