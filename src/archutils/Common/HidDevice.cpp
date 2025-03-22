#include "global.h"
#include "HidDevice.h"
#include "RageLog.h"

HidDevice::HidDevice(int vid, int pid, int interfaceNum, bool autoReconnection, bool nonBlockingWrite) :
	vid{ vid },
	pid{ pid },
	interfaceNum{ interfaceNum },
	autoReconnection{autoReconnection},
	nonBlockingWrite{nonBlockingWrite}
{
	bool result = TryConnect();

	if (!result) {

		LOG->Warn("HID device with VID/PID %04x/%04x not found.", vid, pid);
		return;
	}
	else
		foundOnce = true;
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
		LOG->Info("HidDevice opened %04x:%04x:%d by path %s", vid, pid, interfaceNum, path);

	return handle != nullptr;
}

bool HidDevice::TryConnect()
{
	char* path = GetPath(vid, pid, interfaceNum);

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

char* HidDevice::GetPath(int vid, int pid, int interfaceNumber)
{
	struct hid_device_info* devs, * cur_dev;

	devs = hid_enumerate(vid, pid);
	cur_dev = devs;

	if (devs && cur_dev)
	{
		// Look for the desired devices by iterating connected ones
		while (cur_dev)
		{
			if (cur_dev->vendor_id == vid &&
				cur_dev->product_id == pid)
			{
				if (interfaceNumber == -1)
				{
					return cur_dev->path;
				}
				else
				{
					if(cur_dev->interface_number == interfaceNumber)
						return cur_dev->path;
				}
			}

			cur_dev = cur_dev->next;
		}
	}

	return nullptr;
}

void HidDevice::Read(unsigned char* data, size_t length)
{
	if (!CheckConnection())
		return;
	int result = hid_read(handle, data, length);

	if (result == -1)
	{
		LOG->Warn("HID device with VID/PID %04x/%04x read failed. Fail reason %ls", vid, pid, GetError());
	}
}

void HidDevice::Write(const unsigned char* data, size_t length)
{
	if (!CheckConnection())
		return;

	int result = hid_write(handle, data, length);

	if (result != length)
	{
		LOG->Warn("HID device with VID/PID %04x/%04x write failed. Fail reason %ls", vid, pid, GetError());
		Close();
	}
}
