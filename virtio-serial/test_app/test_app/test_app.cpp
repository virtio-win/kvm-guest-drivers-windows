// test_app.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>

#define PAGE_SIZE 4096

VOID CALLBACK FileIOCompletionRoutine(
  __in  DWORD dwErrorCode,
  __in  DWORD dwNumberOfBytesTransfered,
  __in  LPOVERLAPPED lpOverlapped
)
{
	printf("Write complete with error %d, transfered %d\n", 
		dwErrorCode, 
		dwNumberOfBytesTransfered);

	__asm int 3;
}


int _tmain(int argc, _TCHAR* argv[])
{
	HANDLE hFile;

	hFile = CreateFile ( L"\\\\.\\viosdev",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL, // no SECURITY_ATTRIBUTES structure
                        OPEN_EXISTING, // No special create flags
                        FILE_FLAG_OVERLAPPED,//0, // No special attributes
                        NULL);

	if(INVALID_HANDLE_VALUE == hFile)
	{
		printf("Failed to open the device with error %d\n", GetLastError());
		return 0;
	}


	DWORD size;

	char buffer[PAGE_SIZE*2] = "hello world!\n\r";
	char read_buffer[PAGE_SIZE+1];

/*
	for(int i = 0; i < 512; i++)
	{
		sprintf(buffer, "Hello world %d\n\r ", i );
		if(!WriteFile(hFile,
					  buffer,
					  5012,
					  &size,
					  NULL))
		{
			printf("Error writing to file %d\n", GetLastError());
		}
	}
*/
	HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, L"Just_event");
	
	printf ("Created event %x\n", hEvent);
	BOOL bSomethingToRead = TRUE;
	OVERLAPPED Overlapped;
	OVERLAPPED OverlappedRead;

	while(true)
	{
		printf("\n>");
		gets(buffer);
/*		if(!WriteFile(hFile,
					  buffer,
					  strlen(buffer) + 1,
					  &size,
					  NULL))
		{
			printf("Error writing to file %d\n", GetLastError());
		}*/

		
		memset(&Overlapped, 0, sizeof(OVERLAPPED));

		if(!WriteFileEx(hFile,
						buffer,
						strlen(buffer) + 1,
						&Overlapped,
						FileIOCompletionRoutine))
		{
			printf("Error writing to file %d\n", GetLastError());
		}

		if(bSomethingToRead)
		{
			memset(&OverlappedRead, 0, sizeof(OVERLAPPED));
			OverlappedRead.hEvent = hEvent;

			bSomethingToRead = FALSE;

			if(ReadFile(hFile,
						 read_buffer,
						 PAGE_SIZE,
						 &size,
						 &OverlappedRead))
			{
				read_buffer[size] = '\0';
				bSomethingToRead = TRUE;
				//printf("Got from device %d bytes>>> \n%s\n", size, read_buffer);
				printf("%s", read_buffer);
			}			
		}

		if(WAIT_TIMEOUT != WaitForSingleObject(hEvent, 1000))
		{
			bSomethingToRead = TRUE;

			if (GetOverlappedResult(hFile,
                               &OverlappedRead,
                               &size,
                               TRUE))
			{
				read_buffer[size] = '\0';
				printf("Got from device %d bytes>>> \n%s\n", size, read_buffer);
				

				//				printf("%s", read_buffer);
			}
		}

	}

	CloseHandle(hFile);

	return 0;
}
