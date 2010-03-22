// test_app.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>

#define PAGE_SIZE 4096

int _tmain(int argc, _TCHAR* argv[])
{
	HANDLE hFile;

	hFile = CreateFile ( L"\\\\.\\viosdev",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL, // no SECURITY_ATTRIBUTES structure
                        OPEN_EXISTING, // No special create flags
                        0, // No special attributes
                        NULL);

	if(INVALID_HANDLE_VALUE == hFile)
	{
		printf("Failed to open the device with error %d\n", GetLastError());
		return 0;
	}


	DWORD size;
	char buffer[256] = "hello world!";
	char read_buffer[PAGE_SIZE];


	for(int i = 0; i < 512; i++)
	{
		if(!WriteFile(hFile,
					  buffer,
					  strlen(buffer) + 1,
					  &size,
					  NULL))
		{
			printf("Error writing to file %d\n", GetLastError());
		}
	}

	Sleep(10000);

	if(!ReadFile(hFile,
				 read_buffer,
				 PAGE_SIZE,
				 &size,
				 NULL))
	{
		printf("Error reading from file %d\n", GetLastError());
	}
	else
	{
		read_buffer[size] = '\0';

		printf("Got from device %d bytes>>> \n%s\n", size, read_buffer);
	}

	CloseHandle(hFile);

	return 0;
}

