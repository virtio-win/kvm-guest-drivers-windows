// IVSHMEM-test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#define TEST_START(name) printf("Test: %s...", name)
#define TEST_PASS() printf("PASS\n")
#define TEST_FAIL(reason) printf("FAIL - %s\n", reason)

int main()
{
	HDEVINFO deviceInfoSet;
	DWORD    deviceIndex;

	PSP_DEVICE_INTERFACE_DETAIL_DATA infData = NULL;
	HANDLE   devHandle = INVALID_HANDLE_VALUE;

	deviceInfoSet = SetupDiGetClassDevs(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES | DIGCF_DEVICEINTERFACE);
	SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
	ZeroMemory(&deviceInterfaceData, sizeof(SP_DEVICE_INTERFACE_DATA));
	deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	deviceIndex = 0;
	while (1)
	{
		TEST_START("Find device");
		if (SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &GUID_DEVINTERFACE_IVSHMEM, 0, &deviceInterfaceData) == FALSE)
		{
			DWORD error = GetLastError();
			if (error == ERROR_NO_MORE_ITEMS)
			{
				TEST_FAIL("Unable to enumerate the device, is it attached?");
				break;
			}


			TEST_FAIL("SetupDiEnumDeviceInterfaces failed");
			break;
		}
		TEST_PASS();

		TEST_START("Get device name length");
		DWORD reqSize = 0;
		// this returns false even though we succeed in getting the size
		SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, NULL, 0, &reqSize, NULL);
		if (!reqSize)
		{
			TEST_FAIL("SetupDiGetDeviceInterfaceDetail");
			break;
		}
		TEST_PASS();

		TEST_START("Get device name");
		infData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(reqSize);
		infData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		if (!SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, infData, reqSize, NULL, NULL))
		{
			TEST_FAIL("SetupDiGetDeviceInterfaceDetail");
			break;
		}
		TEST_PASS();

		TEST_START("Open device");
		devHandle = CreateFile(infData->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
		if (devHandle == INVALID_HANDLE_VALUE)
		{
			TEST_FAIL("CreateFile returned INVALID_HANDLE_VALUE");
			break;
		}
		TEST_PASS();

		TEST_START("IOCTL_IVSHMEM_REQUEST_PEERID");
		IVSHMEM_PEERID peer = 0;
		ULONG   ulReturnedLength = 0;

		if (!DeviceIoControl(devHandle, IOCTL_IVSHMEM_REQUEST_PEERID, NULL, 0, &peer, sizeof(IVSHMEM_PEERID), &ulReturnedLength, NULL))
		{
			TEST_FAIL("DeviceIoControl");
			printf("Error 0x%x\n", GetLastError());
			break;
		}
		TEST_PASS();

		printf("Peer: %u\n", peer);

		TEST_START("IOCTL_IVSHMEM_REQUEST_SIZE");
		IVSHMEM_SIZE size = 0;
		if (!DeviceIoControl(devHandle, IOCTL_IVSHMEM_REQUEST_SIZE, NULL, 0, &size, sizeof(IVSHMEM_SIZE), &ulReturnedLength, NULL))
		{
			TEST_FAIL("DeviceIoControl");
			break;
		}

		if (size == 0)
		{
			TEST_FAIL("Size should not be zero");
			break;
		}
		TEST_PASS();

		printf("Size: %I64u\n", size);

		TEST_START("IOCTL_IVSHMEM_REQUEST_MMAP");
		IVSHMEM_MMAP_CONFIG config;
		config.cacheMode = IVSHMEM_CACHE_NONCACHED;
		IVSHMEM_MMAP map;
		ZeroMemory(&map, sizeof(IVSHMEM_MMAP));
		if (!DeviceIoControl(devHandle, IOCTL_IVSHMEM_REQUEST_MMAP, &config, sizeof(IVSHMEM_MMAP_CONFIG), &map, sizeof(IVSHMEM_MMAP), &ulReturnedLength, NULL))
		{
			TEST_FAIL("DeviceIoControl");
			break;
		}

		if (!map.ptr)
		{
			TEST_FAIL("NULL pointer to mapping returned");
			break;
		}

		if (map.size != size)
		{
			TEST_FAIL("Incorrect size");
			break;
		}
		TEST_PASS();

		TEST_START("Mapping more then once fails");
		if (DeviceIoControl(devHandle, IOCTL_IVSHMEM_REQUEST_MMAP, NULL, 0, &map, sizeof(IVSHMEM_MMAP), &ulReturnedLength, NULL))
		{
			TEST_FAIL("mapping succeeded, this should not happen!");
			break;
		}
		TEST_PASS();

		TEST_START("Mapping from another handle fails");
		HANDLE devHandle2 = CreateFile(infData->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
		if (!devHandle2)
		{
			TEST_FAIL("Failed to open second handle");
			break;
		}
		if (DeviceIoControl(devHandle, IOCTL_IVSHMEM_REQUEST_MMAP, NULL, 0, &map, sizeof(IVSHMEM_MMAP), &ulReturnedLength, NULL))
		{
			TEST_FAIL("mapping succeeded, this should not happen!");
			break;
		}
		CloseHandle(devHandle2);
		TEST_PASS();

		TEST_START("IOCTL_IVSHMEM_RING_DOORBELL");
		IVSHMEM_RING ring;
		ring.peerID = 0;
		ring.vector = 0;
		if (!DeviceIoControl(devHandle, IOCTL_IVSHMEM_RING_DOORBELL, &ring, sizeof(IVSHMEM_RING), NULL, 0, &ulReturnedLength, NULL))
		{
			TEST_FAIL("DeviceIoControl");
			break;
		}
		TEST_PASS();

		TEST_START("IOCTL_IVSHMEM_RELEASE_MMAP");
		if (!DeviceIoControl(devHandle, IOCTL_IVSHMEM_RELEASE_MMAP, NULL, 0, NULL, 0, &ulReturnedLength, NULL))
		{
			TEST_FAIL("DeviceIoControl");
			break;
		}
		TEST_PASS();

		TEST_START("Closing handle releases mapping");
		CloseHandle(devHandle);
		devHandle = CreateFile(infData->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
		if (devHandle == INVALID_HANDLE_VALUE)
		{
			TEST_FAIL("Failed to re-open handle");
			break;
		}
		if (!DeviceIoControl(devHandle, IOCTL_IVSHMEM_REQUEST_MMAP, NULL, 0, &map, sizeof(IVSHMEM_MMAP), &ulReturnedLength, NULL))
		{
			TEST_FAIL("Mapping failed!");
			break;
		}
		TEST_PASS();

		TEST_START("Shared memory actually works");
		memset(map.ptr, 0xAA, (size_t)map.size);
		CloseHandle(devHandle);
		devHandle = CreateFile(infData->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
		if (devHandle == INVALID_HANDLE_VALUE)
		{
			TEST_FAIL("Failed to re-open handle");
			break;
		}
		if (!DeviceIoControl(devHandle, IOCTL_IVSHMEM_REQUEST_MMAP, NULL, 0, &map, sizeof(IVSHMEM_MMAP), &ulReturnedLength, NULL))
		{
			TEST_FAIL("Mapping failed!");
			break;
		}

		bool fail = false;
		unsigned char *data = (unsigned char *)map.ptr;
		for (UINT64 i = 0; i < map.size; ++i)
			if (data[i] != 0xAA)
			{
				TEST_FAIL("Invalid data read back");
				fail = true;
				break;
			}
		if (fail)
			break;
		TEST_PASS();

		if (map.vectors > 0)
		{
			TEST_START("Check events work, send interrupt 0 to this peer");
			IVSHMEM_EVENT event;
			event.event = CreateEvent(NULL, TRUE, FALSE, L"TEST_EVENT");
			event.vector = 0;
			event.singleShot = TRUE;
			if (event.event == INVALID_HANDLE_VALUE)
			{
				TEST_FAIL("Failed to create the event");
				break;
			}
			if (!DeviceIoControl(devHandle, IOCTL_IVSHMEM_REGISTER_EVENT, &event, sizeof(IVSHMEM_EVENT), NULL, 0, &ulReturnedLength, NULL))
			{
				TEST_FAIL("Register event failed!");
				CloseHandle(event.event);
				break;
			}

			if (WaitForSingleObject(event.event, INFINITE) != WAIT_OBJECT_0)
			{
				TEST_FAIL("WaitForSingleObject failed!");
				CloseHandle(event.event);
				break;
			}
			CloseHandle(event.event);
			TEST_PASS();
		}

		break;
	}

	if (devHandle != INVALID_HANDLE_VALUE)
		CloseHandle(devHandle);

	if (infData)
		free(infData);

	SetupDiDestroyDeviceInfoList(deviceInfoSet);

    return 0;
}
