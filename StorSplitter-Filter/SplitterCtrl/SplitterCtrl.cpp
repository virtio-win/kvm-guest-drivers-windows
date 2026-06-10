#include <windows.h>
#include <stdio.h>
#include <winioctl.h>

// Point this to the shared header inside your driver project folder
#include "../StorSplitterFilter/SharedIoctls.h"

// ---------------------------------------------------------------------------
// 2. Main CLI Logic
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
	if (argc != 2) 
	{
		printf("Usage: SplitterCtrl.exe [enable | disable | status]\n");
		return 1;
	}

	DWORD ioctlCode = 0;
	const char* actionName = "";

	if (_stricmp(argv[1], "enable") == 0) 
	{
		ioctlCode = IOCTL_SPLITTER_ENABLE;
		actionName = "ENABLE";
	}
	else if (_stricmp(argv[1], "disable") == 0) 
	{
		ioctlCode = IOCTL_SPLITTER_DISABLE;
		actionName = "DISABLE";
	}
	else if (_stricmp(argv[1], "status") == 0) 
	{
		ioctlCode = IOCTL_SPLITTER_QUERY_STATUS;
		actionName = "QUERY_STATUS";
	}
	else {
		printf("Invalid argument. Use 'enable', 'disable', or 'status'.\n");
		return 1;
	}

	HANDLE hDevice = CreateFileA(
		"\\\\.\\StorSplitterCtrl",
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);

	if (hDevice == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		printf("Failed to open device handle (Error %lu).\n", err);
		if (err == ERROR_ACCESS_DENIED) 
			printf("   -> Hint: Run as Administrator!\n");
		return 1;
	}

	DWORD bytesReturned = 0;
	LONG driverStatus = 0; 
	BOOL result = FALSE;

	if (ioctlCode == IOCTL_SPLITTER_QUERY_STATUS) 
	{
		result = DeviceIoControl(
			hDevice, ioctlCode,
			NULL, 0,                           // No input
			&driverStatus, sizeof(driverStatus), // Output buffer & size
			&bytesReturned, NULL
		);

		if (result && bytesReturned == sizeof(LONG)) 
		{
			printf("Splitter is currently: %s\n", driverStatus == 1 ? "ENABLED" : "DISABLED");
		}
		else {
			printf("Failed to query status. Error Code: %lu\n", GetLastError());
		}
	}
	else 
	{
		result = DeviceIoControl(hDevice, ioctlCode, NULL, 0, NULL, 0, &bytesReturned, NULL);

		if (result) printf("SUCCESS!\n");
		else 
			printf("FAILED. Error Code: %lu\n", GetLastError());
	}

	CloseHandle(hDevice);
	return 0;
}
