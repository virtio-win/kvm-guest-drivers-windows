// vioivshmem-test.cpp : Defines the entry point for the console application.
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
		if (SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &GUID_DEVINTERFACE_VIOIVSHMEM, 0, &deviceInterfaceData) == FALSE)
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

		TEST_START("IOCTL_VIOIVSHMEM_REQUEST_SIZE");
		UINT64 size = 0;
		if (!DeviceIoControl(devHandle, IOCTL_VIOIVSHMEM_REQUEST_SIZE, NULL, 0, &size, sizeof(UINT64), NULL, NULL))
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

		printf("Size: %u\n", size);

		TEST_START("IOCTL_VIOIVSHMEM_REQUEST_MMAP");
		VIOIVSHMEM_MMAP map;
		ZeroMemory(&map, sizeof(VIOIVSHMEM_MMAP));
		if (!DeviceIoControl(devHandle, IOCTL_VIOIVSHMEM_REQUEST_MMAP, NULL, 0, &map, sizeof(VIOIVSHMEM_MMAP), NULL, NULL))
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
		if (DeviceIoControl(devHandle, IOCTL_VIOIVSHMEM_REQUEST_MMAP, NULL, 0, &map, sizeof(VIOIVSHMEM_MMAP), NULL, NULL))
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
		if (DeviceIoControl(devHandle, IOCTL_VIOIVSHMEM_REQUEST_MMAP, NULL, 0, &map, sizeof(VIOIVSHMEM_MMAP), NULL, NULL))
		{
			TEST_FAIL("mapping succeeded, this should not happen!");
			break;
		}
		CloseHandle(devHandle2);
		TEST_PASS();

		TEST_START("IOCTL_VIOIVSHMEM_RING_DOORBELL");
		VIOIVSHMEM_RING ring;
		ring.peerID = 0;
		ring.vector = 0;
		if (!DeviceIoControl(devHandle, IOCTL_VIOIVSHMEM_RING_DOORBELL, &ring, sizeof(VIOIVSHMEM_RING), NULL, 0, NULL, NULL))
		{
			TEST_FAIL("DeviceIoControl");
			break;
		}
		TEST_PASS();

		TEST_START("IOCTL_VIOIVSHMEM_RELEASE_MMAP");
		if (!DeviceIoControl(devHandle, IOCTL_VIOIVSHMEM_RELEASE_MMAP, NULL, 0, NULL, 0, NULL, NULL))
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
		if (!DeviceIoControl(devHandle, IOCTL_VIOIVSHMEM_REQUEST_MMAP, NULL, 0, &map, sizeof(VIOIVSHMEM_MMAP), NULL, NULL))
		{
			TEST_FAIL("Mapping failed!");
			break;
		}
		TEST_PASS();

		TEST_START("Shared memory actually works");
		memset(map.ptr, 0xAA, map.size);
		CloseHandle(devHandle);
		devHandle = CreateFile(infData->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
		if (devHandle == INVALID_HANDLE_VALUE)
		{
			TEST_FAIL("Failed to re-open handle");
			break;
		}
		if (!DeviceIoControl(devHandle, IOCTL_VIOIVSHMEM_REQUEST_MMAP, NULL, 0, &map, sizeof(VIOIVSHMEM_MMAP), NULL, NULL))
		{
			TEST_FAIL("Mapping failed!");
			break;
		}

		bool fail = false;
		unsigned char *data = (unsigned char *)map.ptr;
		for(UINT64 i = 0; i < map.size; ++i)
			if (data[i] != 0xAA)
			{
				TEST_FAIL("Invalid data read back");
				fail = true;
				break;
			}
		if (fail)
			break;
		TEST_PASS();

		break;
	}

	if (devHandle != INVALID_HANDLE_VALUE)
		CloseHandle(devHandle);

	if (infData)
		free(infData);

	SetupDiDestroyDeviceInfoList(deviceInfoSet);

    return 0;
}