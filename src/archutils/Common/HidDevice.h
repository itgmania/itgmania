#ifndef HidDevice_H
#define HidDevice_H

#include "hidapi.h"

class HidDevice
{
private:
	hid_device* handle{nullptr};
	char* path = nullptr;
	void Close();
	bool Open();
	bool TryConnect();
public:
	static char* GetPath(int vid, int pid, int interfaceNum = -1);

	HidDevice(int vid, int pid, int interfaceNum = -1);

	virtual ~HidDevice();

	bool IsConnected();
	
	void Read(unsigned char* data, size_t length);
	void Write(const unsigned char* data, size_t length);
};

#endif
