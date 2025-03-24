#ifndef HidDevice_H
#define HidDevice_H

#include "hidapi.h"
#include "vector"

#define FAIL -1
#define NOT_CONNECTED -2

static std::vector<int> make_pids(int base_pid, int size)
{
	std::vector<int> vec(size);

	for (int i = 0; i < size; i++)
		vec[i] = base_pid + i;

	return vec;
}

class HidDevice
{
private:
	hid_device* handle{nullptr};

	static const RString GetPidsString(const std::vector<int> pids);

	int vid;
	const std::vector<int> pids;
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
	static char* GetPath(int vid, const std::vector<int> pids, int interfaceNum = -1);

	HidDevice(int vid, const std::vector<int> pids, int interfaceNum = -1, bool autoReconnection = true, bool nonBlockingWrite = false);
	HidDevice(int vid, int pid, int interfaceNum = -1, bool autoReconnection = true, bool nonBlockingWrite = false);

	virtual ~HidDevice();

	bool IsConnected();
	bool FoundOnce();

	int Read(unsigned char* data, size_t length);
	int Write(const unsigned char* data, size_t length);
};

#endif
