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
  static CocoaEventDispatcher shared_dispatcher_;

  CocoaEventDispatcher();
  CocoaEventDispatcher(const CocoaEventDispatcher&) = delete;
  CocoaEventDispatcher(CocoaEventDispatcher&&) = delete;

  unsigned AddResponder(const std::function<void(EVENT_TYPE)>& resp);
  void RemoveResponder(unsigned resp_id);
  void DispatchEvent(EVENT_TYPE e);

 private:
  struct Responder {
    unsigned id_;
    std::function<void(EVENT_TYPE)> function_;
  };

  RageMutex mutex_;
  std::vector<Responder> responders_;
  unsigned next_responder_id_;
};

#endif  // COCOA_EVENT_DISPATCHER_H
