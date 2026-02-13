#ifndef COCOA_EVENT_DISPATCHER_H
#define COCOA_EVENT_DISPATCHER_H

#include <functional>
#include <vector>

#include "RageThreads.h"

#if __OBJC__
#import <Cocoa/Cocoa.h>
#define EVENT_TYPE NSEvent*
#else
#define EVENT_TYPE void*
#endif

class CocoaEventDispatcher {
 public:
  static CocoaEventDispatcher sharedDispatcher;

  CocoaEventDispatcher();
  CocoaEventDispatcher(const CocoaEventDispatcher&) = delete;
  CocoaEventDispatcher(CocoaEventDispatcher&&) = delete;

  unsigned AddResponder(const std::function<void(EVENT_TYPE)>& resp);
  void RemoveResponder(unsigned respID);
  void DispatchEvent(EVENT_TYPE e);

 private:
  struct Responder {
    unsigned ID;
    std::function<void(EVENT_TYPE)> Function;
  };

  RageMutex m_Mutex;
  std::vector<Responder> m_Responders;
  unsigned m_NextResponderID;
};

#endif  // COCOA_EVENT_DISPATCHER_H
