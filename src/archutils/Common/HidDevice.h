#ifndef HidDevice_H
#define HidDevice_H

#include "hidapi.h"

class HidDevice
{
private:
	hid_device* handle;
	bool foundOnce = false;
	int vid;
	int pid;

	bool TryConnect();
	bool IsConnected();
public:
	HidDevice(int vid, int pid);

	virtual ~HidDevice();

	void Read(unsigned char* data, size_t length);
	void Write(const unsigned char* data, size_t length);
};

#endif
