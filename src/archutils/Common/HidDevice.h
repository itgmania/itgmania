#ifndef HidDevice_H
#define HidDevice_H

#include "hidapi.h"

class HidDevice
{
private:
	hid_device* handle{nullptr};

	static const RString GetPidsString(const  int pids[]);

	int vid;
	const int* pids;
	int interfaceNum = -1;
	bool autoReconnection = true;
	bool nonBlockingWrite = false;

	bool foundOnce = false;
	void Close();
	bool Open(const char* path);
	bool TryConnect();
	bool CheckConnection();
	const wchar_t* GetError();
public:
	static char* GetPath(int vid, const int pids[], int interfaceNum = -1);

	HidDevice(int vid, const int pids[], int interfaceNum = -1, bool autoReconnection = true, bool nonBlockingWrite = false);

	virtual ~HidDevice();

	bool IsConnected();
	bool FoundOnce();

	int Read(unsigned char* data, size_t length);
	void Write(const unsigned char* data, size_t length);
};

#endif
