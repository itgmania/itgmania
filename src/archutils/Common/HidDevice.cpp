#include "global.h"
#include "HidDevice.h"
#include "RageLog.h"

HidDevice::HidDevice(int vid, int pid) :vid(vid), pid(pid)
{
	bool result = TryConnect();

	if (!result) {

		LOG->Warn("HID device with VID/PID %x/%x not found.", vid, pid);
		hid_exit();
		return;
	}
	else
	{
		hid_set_nonblocking(handle, 1);
		foundOnce = true;
	}
}

HidDevice::~HidDevice()
{
	if (handle)
		hid_close(handle);

	hid_exit();
}

bool HidDevice::TryConnect()
{
	handle = hid_open(vid, pid, NULL);

	return handle != NULL;
}

bool HidDevice::IsConnected() {
	if (!handle && foundOnce)
		return TryConnect();

	return handle != NULL;
}

void HidDevice::Read(unsigned char* data, size_t length)
{
	if (!IsConnected())
		return;

	hid_read(handle, data, length);
}

void HidDevice::Write(const unsigned char* data, size_t length)
{
	if (!IsConnected())
		return;

	hid_write(handle, data, length);
}
