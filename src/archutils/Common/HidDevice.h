#ifndef HidDevice_H
#define HidDevice_H

#include "hidapi.h"
#include "vector"

enum HidResults {
	OperationFailed = -1,
	NotConnected = -2,
	Success = 0,
};

static std::vector<int> make_pids(int base_pid, int size)
{
	std::vector<int> vec(size);

	for (int i = 0; i < size; i++)
		vec[i] = base_pid + i;

	return vec;
}

struct HidDeviceInfo {
	char* path{ nullptr };
	int pid;
	int vid;
	int interfaceNum;
};

class HidDevice
{
private:
	HidDeviceInfo foundDeviceInfo{};
	hid_device* handle{ nullptr };

	static const RString GetPidsString(const std::vector<int> pids);

	//Info necessary to search the connected device once a connection attempt is made
	int vid;
	const std::vector<int> pids;
	int interfaceNum = -1;

	//Behaviour configuration
	bool autoReconnection = true;
	bool nonBlockingRead = false;

	bool foundOnce = false;
	void Close();
	bool Open(const char* path);
	bool TryConnect();
	bool CheckConnection();
	const wchar_t* GetError();
public:
	static void GetDeviceInfo(int vid, const std::vector<int> pids, int interfaceNumber, HidDeviceInfo* device_info);

	HidDevice(int vid, const std::vector<int> pids, int interfaceNum = -1, bool autoReconnection = true, bool nonBlockingRead = false);
	HidDevice(int vid, int pid, int interfaceNum = -1, bool autoReconnection = true, bool nonBlockingRead = false);

	virtual ~HidDevice();

	bool IsConnected();
	bool FoundOnce();

	int Read(unsigned char* data, size_t length);
	HidResults Write(const unsigned char* data, size_t length);
};

#endif
