
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <WinSock2.h>
#include <windows.h>
#include "..\..\tools\vendor.ver"
#include "..\inc\install.h"



static BOOL _DeinstallINFSoftware(void)
{
	LSTATUS err = 0;
	HKEY hKey = NULL;
	BOOL ret = FALSE;

	err = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DeviceSetup\\DeviceSoftware", 0, KEY_WRITE, &hKey);
	ret = (err == ERROR_SUCCESS);
	if (!ret) {
		fprintf(stderr, "[ERROR]: Unable to open INF program database registry key: %li\n", err);
		goto Exit;
	}

	err = RegDeleteKeyW(hKey, VIOSOCK_INF_PROGRAM);
	ret = (err == ERROR_SUCCESS);
	if (!ret) {
		fprintf(stderr, "[ERROR]: Unable to delete INF program's registry key: %li\n", err);
	}

	RegCloseKey(hKey);
Exit:
	if (!ret)
		SetLastError(err);

	return ret;
}


static BOOL _SetStringValue(HKEY Key, const wchar_t *Name, const wchar_t *Data)
{
	LSTATUS err = 0;
	BOOL ret = FALSE;

	err = RegSetValueExW(Key, Name, 0, REG_SZ, (PBYTE)Data, ((DWORD)wcslen(Data) + 1)*sizeof(wchar_t));
	ret = (err == ERROR_SUCCESS);
	if (!ret)
		SetLastError(err);

	return ret;
}


static BOOL _SetDWORDValue(HKEY Key, const wchar_t* Name, DWORD Data)
{
	LSTATUS err = 0;
	BOOL ret = FALSE;

	err = RegSetValueExW(Key, Name, 0, REG_DWORD, (PBYTE)&Data, sizeof(Data));
	ret = (err == ERROR_SUCCESS);
	if (!ret)
		SetLastError(err);

	return ret;
}


static BOOL _AddUninstallKey(void)
{
	LSTATUS err = 0;
	HKEY hKey = NULL;
	BOOL ret = FALSE;
	HKEY hSubKey = NULL;
	wchar_t exeName[1024];
	wchar_t wstring[1024];

	memset(exeName, 0, sizeof(exeName));
	ret = (GetModuleFileNameW(NULL, exeName, sizeof(exeName) / sizeof(exeName[0])) != 0);
	if (!ret) {
		err = GetLastError();
		fprintf(stderr, "[ERROR]: Unable to get this EXE file name: %li\n", err);
		goto Exit;
	}

	err = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_WRITE, &hKey);
	ret = (err == ERROR_SUCCESS);
	if (!ret) {
		fprintf(stderr, "[ERROR]: Unable to open the Uninstall database registry key: %li\n", err);
		goto Exit;
	}

	err = RegCreateKeyExW(hKey, VIOSOCK_INF_PROGRAM, 0, NULL, 0, KEY_WRITE, NULL, &hSubKey, NULL);
	ret = (err == ERROR_SUCCESS);
	RegCloseKey(hKey);
	if (!ret) {
		fprintf(stderr, "[ERROR]: Unable to create program's Uninstall key: %li\n", err);
		goto Exit;
	}

	ret = _SetStringValue(hSubKey, L"DisplayName", VIOSOCK_PROTOCOL_STREAM);
	if (!ret) {
		err = GetLastError();
		fprintf(stderr, "[ERROR]: Unable to set program display name: %li\n", err);
		goto CloseSubkey;
	}

	swprintf(wstring, sizeof(wstring)/sizeof(wstring[0]), L"%ls /uninstall", exeName);
	ret = _SetStringValue(hSubKey, L"UninstallString", wstring);
	if (!ret) {
		err = GetLastError();
		fprintf(stderr, "[ERROR]: Unable to set program uninstall string: %li\n", err);
		goto CloseSubkey;
	}

	ret = _SetDWORDValue(hSubKey, L"VersionMajor", VER_PRODUCTMAJORVERSION);
	if (!ret) {
		err = GetLastError();
		fprintf(stderr, "[ERROR]: Unable to set program major version: %li\n", err);
		goto CloseSubkey;
	}

	ret = _SetDWORDValue(hSubKey, L"VersionMinor", VER_PRODUCTMINORVERSION);
	if (!ret) {
		err = GetLastError();
		fprintf(stderr, "[ERROR]: Unable to set program minor version: %li\n", err);
		goto CloseSubkey;
	}

	swprintf(wstring, sizeof(wstring) / sizeof(wstring[0]), L"%u.%u.%u.%u", VER_PRODUCTMAJORVERSION, VER_PRODUCTMINORVERSION, VER_PRODUCTBUILD, VER_PRODUCTBUILD_QFE);
	ret = _SetStringValue(hSubKey, L"DisplayVersion", wstring);
	if (!ret) {
		err = GetLastError();
		fprintf(stderr, "[ERROR]: Unable to set program's display version: %li\n", err);
		goto CloseSubkey;
	}

	swprintf(wstring, sizeof(wstring)/sizeof(wstring[0]), L"%S", VER_COMPANYNAME_STR);
	ret = _SetStringValue(hSubKey, L"Publisher", wstring);
	if (!ret) {
		err = GetLastError();
		fprintf(stderr, "[ERROR]: Unable to set the publisher: %li\n", err);
		goto CloseSubkey;
	}

CloseSubkey:
	RegCloseKey(hSubKey);
Exit:
	if (!ret)
		SetLastError(err);

	return ret;
}


static BOOL _RemoveUninstallKey(void)
{
	LSTATUS err = 0;
	HKEY hKey = NULL;
	BOOL ret = FALSE;

	err = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_WRITE, &hKey);
	ret = (err == ERROR_SUCCESS);
	if (!ret) {
		fprintf(stderr, "[ERROR]: Unable to open the Uninstall database registry key: %li\n", err);
		goto Exit;
	}

	err = RegDeleteKeyW(hKey, VIOSOCK_INF_PROGRAM);
	ret = (err == ERROR_SUCCESS);
	if (!ret) {
		fprintf(stderr, "[ERROR]: Unable to delete programs's Uninstall registry key: %li\n", err);
	}

	RegCloseKey(hKey);
Exit:
	if (!ret)
		SetLastError(err);

	return ret;
}


int main(int argc, char **argv)
{
	int ret = 0;
	WSADATA wsaData = { 0 };

	if (argc != 2) {
		ret = -1;
		fprintf(stderr, "Usage: %s </install|/uninstall>\n", argv[0]);
		goto Exit;
	}

	ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ret != ERROR_SUCCESS) {
		fprintf(stderr, "[ERROR]: Failed to initialize WinSock2: %i\n", ret);
		goto Exit;
	}

	if (_strnicmp(argv[1], "/install", sizeof("/install") - 1) == 0) {
		if (InstallProtocol()) {
			if (!_AddUninstallKey())
				DeinstallProtocol();
		} else {
			ret = WSAGetLastError();
			fprintf(stderr, "[ERROR]: Unable to install the protocol: %i\n", ret);
		}
	} else if (_strnicmp(argv[1], "/uninstall", sizeof("/uninstall") - 1) == 0) {
		DeinstallProtocol();
		_DeinstallINFSoftware();
		_RemoveUninstallKey();
	}

	WSACleanup();
Exit:
	return ret;
}
