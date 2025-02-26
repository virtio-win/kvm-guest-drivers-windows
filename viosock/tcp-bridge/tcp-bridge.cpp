#include <tchar.h>
#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <ws2spi.h>
#include <ws2tcpip.h>

#include "..\inc\vio_sockets.h"


void create_vsock_addr(PSOCKADDR_VM addr)
{
	//addr->svm_cid = VMADDR_CID_ANY;
	//addr->svm_port = VMADDR_PORT_ANY;
	addr->svm_cid = 3;
	addr->svm_port = 22;
}

void create_tcp_addr(PSOCKADDR_IN addr)
{
	addrinfo hints = {};
	addr->sin_family = AF_INET;
	addr->sin_addr.S_un.S_addr = 2130706433; //127.0.0.1
	addr->sin_port = 22; //127.0.0.1
}

struct SocketPair
{
	SOCKET vsock;
	SOCKET tcp;
};

BOOL Recv(const CHAR* dir, SOCKET sock, PCHAR Buffer, DWORD* BufferLen)
{
	int len = recv(sock, Buffer, *BufferLen, 0);

	if (len == SOCKET_ERROR)
	{
		printf(("%s: recv failed: %d\n"), dir, WSAGetLastError());
		return FALSE;
	}
	else
	{
		printf(("%s: recv %d bytes\n"), dir, len);
		if (!len)
		{
			printf(("%s: recv return 0 ok? connection closed\n"), dir);
		}
		else
		{
			*BufferLen = len;
		}
	}
	return TRUE;
}

BOOL Send(const CHAR* dir, SOCKET sock, PCHAR Buffer, DWORD* BufferLen)
{
	while (BufferLen)
	{
		int len = send(sock, (char*)Buffer, *BufferLen, 0);
		if (len == SOCKET_ERROR)
		{
			printf(("%s: send failed: %d\n"), dir, WSAGetLastError());
			return FALSE;
		}
		else if (!len)
		{
			printf(("%s: send return 0 ok? connection closed\n"), dir);
			return TRUE;
		}
		else
		{
			printf(("%s: %d bytes sent\n"), dir, len);
		}
		*BufferLen -= len;
		Buffer += len;
	}
	return TRUE;
}

DWORD WINAPI ThreadV2T(LPVOID lpParam)
{
	SocketPair* sp = (SocketPair*)lpParam;

	CHAR Buffer[0x1000];
	DWORD BufferLen = sizeof(Buffer);

	while (Recv("V -> T", sp->vsock, Buffer, &BufferLen))
	{
		if (!Send("V -> T", sp->tcp, Buffer, &BufferLen))
			break;

		BufferLen = sizeof(Buffer);
	}

	return 0;
}

DWORD WINAPI ThreadT2V(LPVOID lpParam)
{
	SocketPair* sp = (SocketPair*)lpParam;

	CHAR Buffer[0x1000];
	DWORD BufferLen = sizeof(Buffer);

	while (Recv("T -> V", sp->tcp, Buffer, &BufferLen))
	{
		if (!Send("T -> V", sp->vsock, Buffer, &BufferLen))
			break;

		BufferLen = sizeof(Buffer);
	}

	return 0;
}

void StartThreads(SOCKET vsock, SOCKET tcp)
{
	SocketPair SocketPairData;

	DWORD   dwThreadIdArray[2];
	HANDLE  hThreadArray[2];

	SocketPairData.vsock = vsock;
	SocketPairData.tcp = tcp;

	hThreadArray[0] = CreateThread(
		NULL,
		0,
		ThreadV2T,
		(void*)&SocketPairData,
		0,
		&dwThreadIdArray[0]);

	hThreadArray[1] = CreateThread(
		NULL,
		0,
		ThreadT2V,
		(void*)&SocketPairData,
		0,
		&dwThreadIdArray[1]);

	WaitForMultipleObjects(2, hThreadArray, TRUE, INFINITE);

	shutdown(SocketPairData.vsock, SD_BOTH);
	closesocket(SocketPairData.vsock);

	shutdown(SocketPairData.tcp, SD_BOTH);
	closesocket(SocketPairData.tcp);
}



int __cdecl main()
{
	ADDRESS_FAMILY vsock_AF;
	SOCKADDR_VM vsock_addr = { 0 };
	SOCKET vsock_listen_sock = INVALID_SOCKET;
	SOCKET vsock_client_sock = INVALID_SOCKET;

	SOCKADDR_IN tcp_addr = { 0 };
	SOCKET tcp_server_sock = INVALID_SOCKET;
	SOCKET tcp_client_sock = INVALID_SOCKET;


	WSADATA wsaData = { 0 };

	create_vsock_addr(&vsock_addr);
	create_tcp_addr(&tcp_addr);

	int iRes = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (iRes != ERROR_SUCCESS)
	{
		_tprintf(_T("WSAStartup failed: %d\n"), iRes);
		return 1;
	}

	if (vsock_addr.svm_port == VMADDR_PORT_ANY)
	{
		_tprintf(_T("Invalid port for listen\n"));
		return 2;
	}

	vsock_AF = ViosockGetAF();
	if (vsock_AF == AF_UNSPEC)
	{
		_tprintf(_T("ViosockGetAF failed: %d\n"), GetLastError());
		return 3;
	}

	_tprintf(_T("socket(AF_VSOCK, SOCK_STREAM, 0)\n"));
	vsock_listen_sock = socket(vsock_AF, SOCK_STREAM, 0);
	if (vsock_listen_sock != INVALID_SOCKET)
	{
		vsock_addr.svm_family = vsock_AF;

		if (ERROR_SUCCESS == bind(vsock_listen_sock, (struct sockaddr*)&vsock_addr, sizeof(vsock_addr)))
		{
			if (ERROR_SUCCESS == listen(vsock_listen_sock, 10))
			{
				SOCKADDR_VM rAddr = { 0 };
				int len = sizeof(rAddr);

				_tprintf(_T("listen success\n"));

				vsock_client_sock = accept(vsock_listen_sock, (struct sockaddr*)&rAddr, &len);
				if (vsock_client_sock == INVALID_SOCKET)
				{
					_tprintf(_T("accept failed: %d\n"), WSAGetLastError());
				}
				else
				{
					addrinfo hints;
					ZeroMemory(&hints, sizeof(hints));
					hints.ai_family = AF_UNSPEC;
					hints.ai_socktype = SOCK_STREAM;
					hints.ai_protocol = IPPROTO_TCP;

					addrinfo* result = NULL;
					auto iResult = getaddrinfo("127.0.0.1", "22", &hints, &result);

					if (iResult != 0) {
						printf("getaddrinfo failed with error: %d\n", iResult);
						WSACleanup();
						return 12;
					}
					else
					{
						printf("getaddrinfo ok\n");

					}

					for (addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next) { // серверов может быть несколько, поэтому не помешает цикл

						// Create a SOCKET for connecting to server
						tcp_server_sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

						if (tcp_server_sock == INVALID_SOCKET) {
							_tprintf(_T("tcp_server_sock failed: %d\n"), WSAGetLastError());
							WSACleanup();
							return 13;
						}
						else
						{
							printf("socket ok\n");

						}

						// Connect to server
						iResult = connect(tcp_server_sock, ptr->ai_addr, (int)ptr->ai_addrlen);
						if (iResult == SOCKET_ERROR) {
							closesocket(tcp_server_sock);
							tcp_server_sock = INVALID_SOCKET;
							continue;
						}
						else
						{
							printf("connect ok\n");

						}


						break;
					}

					StartThreads(vsock_client_sock, tcp_server_sock);

					shutdown(vsock_client_sock, SD_BOTH);
					closesocket(vsock_client_sock);

					shutdown(tcp_client_sock, SD_BOTH);
					closesocket(tcp_client_sock);
				}
			}
			else
				_tprintf(_T("listen failed: %d\n"), WSAGetLastError());
		}
		else
			_tprintf(_T("bind failed: %d\n"), WSAGetLastError());

		closesocket(vsock_listen_sock);
	}
	else
		_tprintf(_T("socket failed: %d\n"), WSAGetLastError());

	WSACleanup();
	return 0;
}
