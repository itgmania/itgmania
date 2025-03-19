#ifndef HidDevice_H
#define HidDevice_H

#include "hidapi.h"

class HidDevice
{
private:
	hid_device* handle{nullptr};

	bool autoReconnection = true;

	int vid;
	int pid;
	int interfaceNum = -1;

	bool foundOnce = false;
	void Close();
	bool Open(const char* path);
	bool TryConnect();
	bool CheckConnection();
	const wchar_t* GetError();
public:
	static char* GetPath(int vid, int pid, int interfaceNum = -1);

	HidDevice(int vid, int pid, int interfaceNum = -1, bool autoReconnection = true);

	virtual ~HidDevice();

	bool IsConnected();
	
	void Read(unsigned char* data, size_t length);
	void Write(const unsigned char* data, size_t length);
};

#endif
