//
//  InputHandler_NSEvent.hpp
//  StepMania
//
//  Created by heshuimu on 12/22/19.
//

#ifndef InputHandler_NSEvent_hpp
#define InputHandler_NSEvent_hpp

#include "InputHandler.h"
#include "archutils/Darwin/CocoaEventDispatcher.h"

class InputHandler_NSEvent : public InputHandler {
 public:
  InputHandler_NSEvent();
  ~InputHandler_NSEvent();

  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);

 protected:
  void HandleEvent(EVENT_TYPE e);
  void InitKeyCodeMap();

 private:
  DeviceButton ns_key_code_map_[0x100];
  unsigned responder_id_;
};

#endif /* InputHandler_NSEvent_hpp */
