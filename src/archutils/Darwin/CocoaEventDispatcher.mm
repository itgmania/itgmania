#include "CocoaEventDispatcher.h"
#include "global.h"

CocoaEventDispatcher CocoaEventDispatcher::shared_dispatcher_;

CocoaEventDispatcher::CocoaEventDispatcher()
    : mutex_("Cocoa Event Dispatcher"), next_responder_id_(1) {}

unsigned CocoaEventDispatcher::AddResponder(const std::function<void(EVENT_TYPE)>& resp) {
  LockMut(mutex_);

  const unsigned respID = next_responder_id_++;
  responders_.push_back(CocoaEventDispatcher::Responder{respID, resp});

  return respID;
}

void CocoaEventDispatcher::RemoveResponder(unsigned respID) {
  LockMut(mutex_);
  responders_.erase(find_if(responders_.begin(), responders_.end(), [=](const auto& resp) {
    return resp.id_ == respID;
  }));
}

void CocoaEventDispatcher::DispatchEvent(EVENT_TYPE e) {
  LockMut(mutex_);

  for (const auto& responder : responders_) {
    responder.function_(e);
  }
}
