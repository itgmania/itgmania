#include "global.h"
#include "HidDevice.h"
#include "RageLog.h"

HidDevice::HidDevice(int vid, const std::vector<int> pids, int interfaceNum, bool autoReconnection, bool nonBlockingWrite) :
	vid{ vid },
	pids{ pids },
	interfaceNum{ interfaceNum },
	autoReconnection{ autoReconnection },
	nonBlockingWrite{ nonBlockingWrite }
{
	bool result = TryConnect();

	if (!result)
	{
		LOG->Warn("HidDevice %04x:%s: %d not found", vid, GetPidsString(pids).c_str(), interfaceNum);
		return;
	}
	else
		foundOnce = true;
}

HidDevice::HidDevice(int vid, int pid, int interfaceNum, bool autoReconnection, bool nonBlockingWrite) :
	HidDevice(vid, make_pids(pid, 1), interfaceNum, autoReconnection, nonBlockingWrite)
{
}

HidDevice::~HidDevice()
{
	Close();
	hid_exit();
}

void HidDevice::Close()
{
	hid_close(handle);
	handle = nullptr;
}

bool HidDevice::Open(const char* path)
{
	handle = hid_open_path(path);

	if(nonBlockingWrite)
		hid_set_nonblocking(handle, 1);

	if(handle)
		LOG->Info("HidDevice %04x:%s: %d opened by path %s", vid, GetPidsString(pids).c_str(), interfaceNum, path);

	return handle != nullptr;
}

bool HidDevice::TryConnect()
{
	char* path = GetPath(vid, pids, interfaceNum);

	if (path == nullptr)
		return false;

	return Open(path);
}

bool HidDevice::CheckConnection()
{
	if (IsConnected())
		return true;

	if (!autoReconnection || !foundOnce)
		return false;

	return TryConnect();
}

const wchar_t* HidDevice::GetError()
{
	return hid_read_error(handle);
}

bool HidDevice::IsConnected() {
	return handle != nullptr;
}

bool HidDevice::FoundOnce()
{
	return foundOnce;
}

const RString HidDevice::GetPidsString(const std::vector<int> pids)
{
	RString pidsString;
	char pid[5] = { 0 };
	size_t size = pids.size();

	for (size_t i = 0; i < size; ++i)
	{
		sprintf(pid, "%04X", pids[i]);
		pidsString += pid;

		if (i != size - 1)
			pidsString += ",";
	}

	return pidsString;
}

char* HidDevice::GetPath(int vid, const std::vector<int> pids, int interfaceNumber)
{
	struct hid_device_info* devs, * cur_dev;
	size_t size = pids.size();

	devs = hid_enumerate(vid, 0);
	cur_dev = devs;

	if (devs && cur_dev)
	{
		// Look for the desired devices by iterating connected ones
		while (cur_dev)
		{
			for (int i = 0; i < size; i++)
			{
				if (cur_dev->vendor_id == vid &&
					cur_dev->product_id == pids[i])
				{
					if (interfaceNumber == -1)
					{
						return cur_dev->path;
					}
					else
					{
						if (cur_dev->interface_number == interfaceNumber)
							return cur_dev->path;
					}
				}
			}

			cur_dev = cur_dev->next;
		}
	}

	return nullptr;
}

int HidDevice::Read(unsigned char* data, size_t length)
{
	if (!CheckConnection())
		return NOT_CONNECTED;

	int result = hid_read(handle, data, length);

	if (result == FAIL)
	{
		LOG->Warn("HidDevice %04x:%s: %d read failed. Fail reason %ls", vid, GetPidsString(pids).c_str(), interfaceNum, GetError());
	}

	return result;
}

int HidDevice::Write(const unsigned char* data, size_t length)
{
	if (!CheckConnection())
		return NOT_CONNECTED;

	int result = hid_write(handle, data, length);

	if (result != length)
	{
		LOG->Warn("HidDevice %04x:%s: %d write failed. Fail reason %ls", vid, GetPidsString(pids).c_str(), interfaceNum, GetError());
		Close();
	}

	return result;
}
