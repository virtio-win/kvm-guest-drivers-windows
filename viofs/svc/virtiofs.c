/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma warning(push)
// '<anonymous-tag>' : structure was padded due to alignment specifier
#pragma warning(disable: 4324) 
// nonstandard extension used : nameless struct / union
#pragma warning(disable: 4201)
// potentially uninitialized local variable 'Size' used
#pragma warning(disable: 4701)
#include "winfsp/winfsp.h"
#pragma warning(pop)

#include <aclapi.h>
#include <fcntl.h>
#include <initguid.h>
#include <lm.h>
#include <setupapi.h>
#include <stdio.h>
#include <sys/stat.h>
#include <wtsapi32.h>

#include "virtfs.h"
#include "fusereq.h"

#define FS_SERVICE_NAME TEXT("VirtIO-FS")
#define ALLOCATION_UNIT 4096

#define INVALID_FILE_HANDLE ((uint64_t)(-1))

// Some of the constants defined in Windows doesn't match the values that are
// used in Linux. Don't try just to understand, just redefine them to match.
#undef O_DIRECTORY
#undef O_EXCL
#undef S_IFMT
#undef S_IFDIR

#define O_DIRECTORY 0200000
#define O_EXCL      0200
#define S_IFMT      0170000
#define S_IFDIR     040000

#define DBG(format, ...) \
    FspDebugLog("*** %s: " format "\n", __FUNCTION__, __VA_ARGS__)

#define SafeHeapFree(p) if (p != NULL) { HeapFree(GetProcessHeap(), 0, p); }

#define ReadAndExecute(x) ((x) | (((x) & 0444) >> 2))

typedef struct
{
    FSP_FILE_SYSTEM *FileSystem;
    
    HANDLE  Device;

    // A write request buffer size must not exceed this value.
    UINT32  MaxWrite;

    // Uid/Gid used to describe files' owner on the guest side.
    UINT32  LocalUid;
    UINT32  LocalGid;

    // Uid/Gid used to describe files' owner on the host side.
    UINT32  OwnerUid;
    UINT32  OwnerGid;

} VIRTFS;

typedef struct
{
    PVOID   DirBuffer;
    BOOLEAN IsDirectory;

    uint64_t NodeId;
    uint64_t FileHandle;

} VIRTFS_FILE_CONTEXT, *PVIRTFS_FILE_CONTEXT;

static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime,
    UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO *FileInfo);

static NTSTATUS SetFileSize(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo);

static int64_t GetUniqueIdentifier()
{
    static int64_t uniq = 1;

    return InterlockedIncrement64(&uniq);
}

static void FUSE_HEADER_INIT(struct fuse_in_header *hdr, uint32_t opcode,
    uint64_t nodeid, uint32_t datalen)
{
    hdr->len = sizeof(*hdr) + datalen;
    hdr->opcode = opcode;
    hdr->unique = GetUniqueIdentifier();
    hdr->nodeid = nodeid;
    hdr->uid = 0;
    hdr->gid = 0;
    hdr->pid = GetCurrentProcessId();
}

static VOID VirtFsDelete(VIRTFS *VirtFs)
{
    if (VirtFs->FileSystem != NULL)
    {
        FspFileSystemDelete(VirtFs->FileSystem);
        VirtFs->FileSystem = NULL;
    }

    if (VirtFs->Device != INVALID_HANDLE_VALUE)
    {
        CloseHandle(VirtFs->Device);
        VirtFs->Device = INVALID_HANDLE_VALUE;
    }

    SafeHeapFree(VirtFs);
}

static NTSTATUS FindDeviceInterface(PHANDLE Device)
{
    WCHAR DevicePath[MAX_PATH];
    HDEVINFO DevInfo;
    SECURITY_ATTRIBUTES SecurityAttributes;
    SP_DEVICE_INTERFACE_DATA DevIfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA DevIfaceDetail = NULL;
    ULONG Length, RequiredLength = 0;
    BOOL Result;

    DevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_VIRT_FS, NULL, NULL,
        (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

    if (DevInfo == INVALID_HANDLE_VALUE)
    {
        return FspNtStatusFromWin32(GetLastError());;
    }

    DevIfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    Result = SetupDiEnumDeviceInterfaces(DevInfo, 0,
        &GUID_DEVINTERFACE_VIRT_FS, 0, &DevIfaceData);

    if (Result == FALSE)
    {
        SetupDiDestroyDeviceInfoList(DevInfo);
        return FspNtStatusFromWin32(GetLastError());
    }

    SetupDiGetDeviceInterfaceDetail(DevInfo, &DevIfaceData, NULL, 0,
        &RequiredLength, NULL);

    DevIfaceDetail = LocalAlloc(LMEM_FIXED, RequiredLength);
    if (DevIfaceDetail == NULL)
    {
        SetupDiDestroyDeviceInfoList(DevInfo);
        return FspNtStatusFromWin32(GetLastError());
    }

    DevIfaceDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
    Length = RequiredLength;

    Result = SetupDiGetDeviceInterfaceDetail(DevInfo, &DevIfaceData,
        DevIfaceDetail, Length, &RequiredLength, NULL);

    if (Result == FALSE)
    {
        LocalFree(DevIfaceDetail);
        SetupDiDestroyDeviceInfoList(DevInfo);
        return FspNtStatusFromWin32(GetLastError());
    }

    wcscpy_s(DevicePath, sizeof(DevicePath) / sizeof(WCHAR),
        DevIfaceDetail->DevicePath);

    LocalFree(DevIfaceDetail);
    SetupDiDestroyDeviceInfoList(DevInfo);

    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.lpSecurityDescriptor = NULL;
    SecurityAttributes.bInheritHandle = FALSE;

    *Device = CreateFile(DevicePath, GENERIC_READ | GENERIC_WRITE,
        0, &SecurityAttributes, OPEN_EXISTING, 0L, NULL);

    if (*Device == INVALID_HANDLE_VALUE)
    {
        return FspNtStatusFromWin32(GetLastError());
    }

    return STATUS_SUCCESS;
}

static VOID UpdateLocalUidGid(VIRTFS *VirtFs, DWORD SessionId)
{
    PWSTR UserName = NULL;
    LPUSER_INFO_3 UserInfo = NULL;
    DWORD BytesReturned;
    NET_API_STATUS Status;
    BOOL Result;

    Result = WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, SessionId,
        WTSUserName, &UserName, &BytesReturned);

    if (Result == TRUE)
    {
        Status = NetUserGetInfo(NULL, UserName, 3, (LPBYTE *)&UserInfo);

        if (Status == NERR_Success)
        {
            // Use an account from local machine's user DB as the file's
            // owner (0x30000 + RID).
            VirtFs->LocalUid = UserInfo->usri3_user_id + 0x30000;
            VirtFs->LocalGid = UserInfo->usri3_primary_group_id + 0x30000;
        }

        if (UserInfo != NULL)
        {
            NetApiBufferFree(UserInfo);
        }
    }

    if (UserName != NULL)
    {
        WTSFreeMemory(UserName);
    }
}

static UINT32 PosixUnixModeToAttributes(uint32_t mode)
{
    UINT32 Attributes;

    switch (mode & S_IFMT)
    {
        case S_IFDIR:
            Attributes = FILE_ATTRIBUTE_DIRECTORY;
            break;

        default:
            Attributes = FILE_ATTRIBUTE_ARCHIVE;
            break;
    }

    if (!!(mode & 0222) == FALSE)
    {
        Attributes |= FILE_ATTRIBUTE_READONLY;
    }

    return Attributes;
}

static uint32_t AccessToUnixFlags(UINT32 GrantedAccess)
{
    uint32_t flags;

    switch (GrantedAccess & (FILE_READ_DATA | FILE_WRITE_DATA))
    {
        case FILE_WRITE_DATA:
            flags = O_WRONLY;
            break;
        case FILE_READ_DATA | FILE_WRITE_DATA:
            flags = O_RDWR;
            break;
        case FILE_READ_DATA:
            __fallthrough;
        default:
            flags = O_RDONLY;
            break;
    }

    if ((GrantedAccess & FILE_APPEND_DATA) && (flags == 0))
    {
        flags = O_RDWR;
    }

    return flags;
}

static VOID FileTimeToUnixTime(UINT64 FileTime, uint64_t *time,
    uint32_t *nsec)
{
    __int3264 UnixTime[2];
    FspPosixFileTimeToUnixTime(FileTime, UnixTime);
    *time = UnixTime[0];
    *nsec = (uint32_t)UnixTime[1];
}

static VOID UnixTimeToFileTime(uint64_t time, uint32_t nsec,
    PUINT64 PFileTime)
{
    __int3264 UnixTime[2];
    UnixTime[0] = time;
    UnixTime[1] = nsec;
    FspPosixUnixTimeToFileTime(UnixTime, PFileTime);
}

static VOID SetFileInfo(struct fuse_attr *attr, FSP_FSCTL_FILE_INFO *FileInfo)
{
    FileInfo->FileAttributes = PosixUnixModeToAttributes(attr->mode);
    FileInfo->ReparseTag = 0;
    FileInfo->AllocationSize = attr->blocks * 512;
    FileInfo->FileSize = attr->size;
    UnixTimeToFileTime(attr->ctime, attr->ctimensec, &FileInfo->CreationTime);
    UnixTimeToFileTime(attr->atime, attr->atimensec,
        &FileInfo->LastAccessTime);
    UnixTimeToFileTime(attr->mtime, attr->mtimensec,
        &FileInfo->LastWriteTime);
    FileInfo->ChangeTime = FileInfo->LastWriteTime;
    FileInfo->IndexNumber = 0;
    FileInfo->HardLinks = 0;
    FileInfo->EaSize = 0;

    DBG("ino=%Iu size=%Iu blocks=%Iu atime=%Iu mtime=%Iu ctime=%Iu "
        "atimensec=%u mtimensec=%u ctimensec=%u mode=%x nlink=%u uid=%u "
        "gid=%u rdev=%u blksize=%u", attr->ino, attr->size, attr->blocks,
        attr->atime, attr->mtime, attr->ctime, attr->atimensec,
        attr->mtimensec, attr->ctimensec, attr->mode, attr->nlink,
        attr->uid, attr->gid, attr->rdev, attr->blksize);
}

static NTSTATUS VirtFsFuseRequest(HANDLE Device, LPVOID InBuffer,
    DWORD InBufferSize, LPVOID OutBuffer, DWORD OutBufferSize)
{
    NTSTATUS Status = STATUS_SUCCESS;
    DWORD BytesReturned = 0;
    BOOL Result;
    struct fuse_in_header *in_hdr = InBuffer;
    struct fuse_out_header *out_hdr = OutBuffer;

    DBG(">>req: %d unique: %Iu len: %u", in_hdr->opcode, in_hdr->unique,
        in_hdr->len);

    Result = DeviceIoControl(Device, IOCTL_VIRTFS_FUSE_REQUEST,
        InBuffer, InBufferSize, OutBuffer, OutBufferSize,
        &BytesReturned, NULL);

    if (Result == FALSE)
    {
        return FspNtStatusFromWin32(GetLastError());
    }

    DBG("<<len: %u error: %d unique: %Iu", out_hdr->len, out_hdr->error,
        out_hdr->unique);

    if (BytesReturned != out_hdr->len)
    {
        DBG("BytesReturned != hdr->len");
    }

    if ((BytesReturned != sizeof(struct fuse_out_header)) &&
        (BytesReturned < OutBufferSize))
    {
        DBG("Bytes Returned: %d Expected: %d", BytesReturned, OutBufferSize);
        // XXX return STATUS_UNSUCCESSFUL;
    }

    if (out_hdr->error < 0)
    {
        switch (out_hdr->error)
        {
            case -EPERM:
                Status = STATUS_ACCESS_DENIED;
                break;
            case -ENOENT:
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            case -EIO:
                Status = STATUS_IO_DEVICE_ERROR;
                break;
            case -EBADF:
                Status = STATUS_OBJECT_NAME_INVALID;
                break;
            case -ENOMEM:
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            case -EEXIST:
                Status = STATUS_OBJECT_NAME_COLLISION;
                break;
            case -EINVAL:
                Status = STATUS_INVALID_PARAMETER;
                break;
            case -ENAMETOOLONG:
                Status = STATUS_NAME_TOO_LONG;
                break;
            case -ENOSYS:
                Status = STATUS_NOT_IMPLEMENTED;
                break;
            case -EOPNOTSUPP:
                Status = STATUS_NOT_SUPPORTED;
                break;
            default:
                Status = STATUS_UNSUCCESSFUL;
                break;
        }
    }

    return Status;
}

static NTSTATUS VirtFsCreateFile(VIRTFS *VirtFs,
    VIRTFS_FILE_CONTEXT *FileContext, UINT32 GrantedAccess, CHAR *FileName,
    UINT64 Parent, UINT32 Mode, UINT64 AllocationSize,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    NTSTATUS Status;
    FUSE_CREATE_IN create_in;
    FUSE_CREATE_OUT create_out;

    FUSE_HEADER_INIT(&create_in.hdr, FUSE_CREATE, Parent,
        sizeof(struct fuse_create_in) + lstrlenA(FileName) + 1);

    create_in.hdr.uid = VirtFs->OwnerUid;
    create_in.hdr.gid = VirtFs->OwnerGid;

    lstrcpyA(create_in.name, FileName);
    create_in.create.mode = Mode;
    create_in.create.umask = 0;
    create_in.create.flags = AccessToUnixFlags(GrantedAccess) | O_EXCL;

    DBG("create_in.create.flags: 0x%08x", create_in.create.flags);
    DBG("create_in.create.mode: 0x%08x", create_in.create.mode);

    Status = VirtFsFuseRequest(VirtFs->Device, &create_in, create_in.hdr.len,
        &create_out, sizeof(create_out));

    if (NT_SUCCESS(Status))
    {
        FileContext->NodeId = create_out.entry.nodeid;
        FileContext->FileHandle = create_out.open.fh;

        if (AllocationSize > 0)
        {
            Status = SetFileSize(VirtFs->FileSystem, FileContext,
                AllocationSize, TRUE, FileInfo);
        }
        else
        {
            SetFileInfo(&create_out.entry.attr, FileInfo);
        }
    }

    return Status;
}

static NTSTATUS VirtFsCreateDir(VIRTFS *VirtFs,
    VIRTFS_FILE_CONTEXT *FileContext, CHAR *FileName, UINT64 Parent,
    UINT32 Mode, FSP_FSCTL_FILE_INFO *FileInfo)
{
    NTSTATUS Status;
    FUSE_MKDIR_IN mkdir_in;
    FUSE_MKDIR_OUT mkdir_out;

    FUSE_HEADER_INIT(&mkdir_in.hdr, FUSE_MKDIR, Parent,
        sizeof(struct fuse_mkdir_in) + lstrlenA(FileName) + 1);

    mkdir_in.hdr.uid = VirtFs->OwnerUid;
    mkdir_in.hdr.gid = VirtFs->OwnerGid;

    lstrcpyA(mkdir_in.name, FileName);
    mkdir_in.mkdir.mode = Mode | 0111; /* ---x--x--x */
    mkdir_in.mkdir.umask = 0;

    Status = VirtFsFuseRequest(VirtFs->Device, &mkdir_in, mkdir_in.hdr.len,
        &mkdir_out, sizeof(mkdir_out));

    if (NT_SUCCESS(Status))
    {
        FileContext->NodeId = mkdir_out.entry.nodeid;
        SetFileInfo(&mkdir_out.entry.attr, FileInfo);
    }

    return Status;
}

static VOID SubmitDeleteRequest(HANDLE Device,
    VIRTFS_FILE_CONTEXT *FileContext, CHAR *FileName, UINT64 Parent)
{
    FUSE_UNLINK_IN unlink_in;
    FUSE_UNLINK_OUT unlink_out;

    FUSE_HEADER_INIT(&unlink_in.hdr,
        FileContext->IsDirectory ? FUSE_RMDIR : FUSE_UNLINK, Parent,
        lstrlenA(FileName) + 1);

    lstrcpyA(unlink_in.name, FileName);

    (VOID)VirtFsFuseRequest(Device, &unlink_in, unlink_in.hdr.len,
        &unlink_out, sizeof(unlink_out));
}

static NTSTATUS SubmitLookupRequest(HANDLE Device, uint64_t parent,
    char *filename, FUSE_LOOKUP_OUT *LookupOut)
{
    NTSTATUS Status;
    FUSE_LOOKUP_IN lookup_in;

    FUSE_HEADER_INIT(&lookup_in.hdr, FUSE_LOOKUP, parent,
        lstrlenA(filename) + 1);

    lstrcpyA(lookup_in.name, filename);

    Status = VirtFsFuseRequest(Device, &lookup_in, lookup_in.hdr.len,
        LookupOut, sizeof(*LookupOut));

    if (NT_SUCCESS(Status))
    {
        struct fuse_attr *attr = &LookupOut->entry.attr;

        DBG("nodeid=%Iu ino=%Iu size=%Iu blocks=%Iu atime=%Iu mtime=%Iu "
            "ctime=%Iu atimensec=%u mtimensec=%u ctimensec=%u mode=%x "
            "nlink=%u uid=%u gid=%u rdev=%u blksize=%u",
            LookupOut->entry.nodeid, attr->ino, attr->size, attr->blocks,
            attr->atime, attr->mtime, attr->ctime, attr->atimensec,
            attr->mtimensec, attr->ctimensec, attr->mode, attr->nlink,
            attr->uid, attr->gid, attr->rdev, attr->blksize);
    }

    return Status;
}

static NTSTATUS PathWalkthough(HANDLE Device, CHAR *FullPath,
    CHAR **FileName, UINT64 *Parent)
{
    NTSTATUS Status = STATUS_SUCCESS;
    FUSE_LOOKUP_OUT LookupOut;
    CHAR *Separator;

    *Parent = FUSE_ROOT_ID;
    *FileName = FullPath;

    while ((Separator = strchr(*FileName, '/')) != NULL)
    {
        *Separator = '\0';

        Status = SubmitLookupRequest(Device, *Parent, *FileName, &LookupOut);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        *Parent = LookupOut.entry.nodeid;
        *FileName = Separator + 1;
    }

    return Status;
}

static NTSTATUS VirtFsLookupFileName(HANDLE Device, PWSTR FileName,
    FUSE_LOOKUP_OUT *LookupOut)
{
    NTSTATUS Status;
    char *filename, *fullpath;
    uint64_t parent;

    if (lstrcmp(FileName, TEXT("\\")) == 0)
    {
        FileName = TEXT("\\.");
    }

    Status = FspPosixMapWindowsToPosixPath(FileName + 1, &fullpath);
    if (!NT_SUCCESS(Status))
    {
        FspPosixDeletePath(fullpath);
        return Status;
    }

    Status = PathWalkthough(Device, fullpath, &filename, &parent);
    if (NT_SUCCESS(Status))
    {
        Status = SubmitLookupRequest(Device, parent, filename, LookupOut);
    }

    FspPosixDeletePath(fullpath);

    return Status;
}

static NTSTATUS GetFileInfoInternal(VIRTFS *VirtFs,
    PVIRTFS_FILE_CONTEXT FileContext, FSP_FSCTL_FILE_INFO *FileInfo,
    PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    NTSTATUS Status;
    FUSE_GETATTR_IN getattr_in;
    FUSE_GETATTR_OUT getattr_out;

    if ((FileInfo != NULL) && (SecurityDescriptor != NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    FUSE_HEADER_INIT(&getattr_in.hdr, FUSE_GETATTR, FileContext->NodeId,
        sizeof(getattr_in.getattr));

    if (FileContext->FileHandle != INVALID_FILE_HANDLE)
    {
        getattr_in.getattr.fh = FileContext->FileHandle;
        getattr_in.getattr.getattr_flags |= FUSE_GETATTR_FH;
    }

    getattr_in.getattr.getattr_flags = 0;
        
    Status = VirtFsFuseRequest(VirtFs->Device, &getattr_in,
        sizeof(getattr_in), &getattr_out, sizeof(getattr_out));

    if (NT_SUCCESS(Status))
    {
        struct fuse_attr *attr = &getattr_out.attr.attr;

        if (FileInfo != NULL)
        {
            SetFileInfo(attr, FileInfo);
        }

        if (SecurityDescriptor != NULL)
        {
            Status = FspPosixMapPermissionsToSecurityDescriptor(
                VirtFs->LocalUid, VirtFs->LocalGid,
                ReadAndExecute(attr->mode), SecurityDescriptor);
        }
    }

    return Status;
}

static VOID GetVolumeName(HANDLE Device, PWSTR VolumeName,
    DWORD VolumeNameSize)
{
    DWORD BytesReturned;
    BOOL Result;

    Result = DeviceIoControl(Device, IOCTL_VIRTFS_GET_VOLUME_NAME, NULL, 0,
        VolumeName, VolumeNameSize, &BytesReturned, NULL);

    if (Result == FALSE)
    {
        lstrcpy(VolumeName, L"Default");
    }
}

static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_VOLUME_INFO *VolumeInfo)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    NTSTATUS Status;
    FUSE_STATFS_IN statfs_in;
    FUSE_STATFS_OUT statfs_out;

    FUSE_HEADER_INIT(&statfs_in.hdr, FUSE_STATFS, FUSE_ROOT_ID, 0);

    Status = VirtFsFuseRequest(VirtFs->Device, &statfs_in, sizeof(statfs_in),
        &statfs_out, sizeof(statfs_out));

    if (NT_SUCCESS(Status))
    {
        struct fuse_kstatfs *kstatfs = &statfs_out.statfs.st;

        VolumeInfo->TotalSize = kstatfs->bsize * kstatfs->blocks;
        VolumeInfo->FreeSize = kstatfs->bsize * kstatfs->bavail;

        GetVolumeName(VirtFs->Device, VolumeInfo->VolumeLabel,
            sizeof(VolumeInfo->VolumeLabel));

        VolumeInfo->VolumeLabelLength = 
            (UINT16)(wcslen(VolumeInfo->VolumeLabel) * sizeof(WCHAR));
    }

    DBG("VolumeLabel: %S", VolumeInfo->VolumeLabel);

    return Status;
}

static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName,
    PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor,
    SIZE_T *PSecurityDescriptorSize)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    PSECURITY_DESCRIPTOR Security = NULL;
    DWORD SecuritySize;
    NTSTATUS Status;
    FUSE_LOOKUP_OUT lookup_out;

    DBG("\"%S\"", FileName);

    Status = VirtFsLookupFileName(VirtFs->Device, FileName, &lookup_out);
    if (NT_SUCCESS(Status))
    {
        struct fuse_attr *attr = &lookup_out.entry.attr;

        if (lstrcmp(FileName, TEXT("\\")) == 0)
        {
            VirtFs->OwnerUid = attr->uid;
            VirtFs->OwnerGid = attr->gid;
        }

        if (PFileAttributes != NULL)
        {
            *PFileAttributes = PosixUnixModeToAttributes(attr->mode);
        }

        Status = FspPosixMapPermissionsToSecurityDescriptor(VirtFs->LocalUid,
            VirtFs->LocalGid, ReadAndExecute(attr->mode), &Security);

        if (NT_SUCCESS(Status))
        {
            SecuritySize = GetSecurityDescriptorLength(Security);

            if ((PSecurityDescriptorSize != NULL) &&
                (*PSecurityDescriptorSize < SecuritySize))
            {
                Status = STATUS_BUFFER_OVERFLOW;
            }
            else
            {
                if (SecurityDescriptor != NULL)
                {
                    memcpy(SecurityDescriptor, Security, SecuritySize);
                }
            }
            SafeHeapFree(Security);
        }
        else
        {
            SecuritySize = 0;
        }

        if (PSecurityDescriptorSize != NULL)
        {
            *PSecurityDescriptorSize = SecuritySize;
        }
    }

    return Status;
}

static NTSTATUS Create(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName,
    UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes,
    PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
    PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext;
    NTSTATUS Status;
    UINT32 Mode = 0664 /* -rw-rw-r-- */;
    char *filename, *fullpath;
    uint64_t parent;

    UNREFERENCED_PARAMETER(SecurityDescriptor);

    DBG("\"%S\" CreateOptions: 0x%08x GrantedAccess: 0x%08x "
        "FileAttributes: 0x%08x AllocationSize: %Iu", FileName,
        CreateOptions, GrantedAccess, FileAttributes, AllocationSize);

    Status = FspPosixMapWindowsToPosixPath(FileName + 1, &fullpath);
    if (!NT_SUCCESS(Status))
    {
        FspPosixDeletePath(fullpath);
        return Status;
    }

    Status = PathWalkthough(VirtFs->Device, fullpath, &filename, &parent);
    if (!NT_SUCCESS(Status) && (Status != STATUS_OBJECT_NAME_NOT_FOUND))
    {
        FspPosixDeletePath(fullpath);
        return Status;
    }

    FileContext = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*FileContext));

    if (FileContext == NULL)
    {
        FspPosixDeletePath(fullpath);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    FileContext->IsDirectory = !!(CreateOptions & FILE_DIRECTORY_FILE);
    FileContext->FileHandle = INVALID_FILE_HANDLE;

    if (!!(FileAttributes & FILE_ATTRIBUTE_READONLY) == TRUE)
    {
        Mode &= ~0222;
    }

    if (FileContext->IsDirectory == TRUE)
    {
        Status = VirtFsCreateDir(VirtFs, FileContext, filename, parent, Mode,
            FileInfo);
    }
    else
    {
        Status = VirtFsCreateFile(VirtFs, FileContext, GrantedAccess,
            filename, parent, Mode, AllocationSize, FileInfo);
    }

    FspPosixDeletePath(fullpath);

    if (!NT_SUCCESS(Status))
    {
        SafeHeapFree(FileContext);
        return Status;
    }

    *PFileContext = FileContext;

    return Status;
}

static NTSTATUS Open(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName,
    UINT32 CreateOptions, UINT32 GrantedAccess, PVOID *PFileContext,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext;
    NTSTATUS Status;
    FUSE_LOOKUP_OUT lookup_out;
    FUSE_OPEN_IN open_in;
    FUSE_OPEN_OUT open_out;

    DBG("\"%S\" CreateOptions: 0x%08x GrantedAccess: 0x%08x", FileName,
        CreateOptions, GrantedAccess);

    FileContext = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*FileContext));

    if (FileContext == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = VirtFsLookupFileName(VirtFs->Device, FileName, &lookup_out);
    if (!NT_SUCCESS(Status))
    {
        SafeHeapFree(FileContext);
        return Status;
    }

    FileContext->IsDirectory = !!(lookup_out.entry.attr.mode & S_IFDIR);

    FUSE_HEADER_INIT(&open_in.hdr,
        (FileContext->IsDirectory == TRUE) ? FUSE_OPENDIR : FUSE_OPEN,
        lookup_out.entry.nodeid, sizeof(open_in.open));

    open_in.open.flags = AccessToUnixFlags(GrantedAccess);

    if (FileContext->IsDirectory == TRUE)
    {
        open_in.open.flags |= O_DIRECTORY;
    }

    Status = VirtFsFuseRequest(VirtFs->Device, &open_in, sizeof(open_in),
        &open_out, sizeof(open_out));

    if (!NT_SUCCESS(Status))
    {
        SafeHeapFree(FileContext);
        return Status;
    }

    FileContext->NodeId = lookup_out.entry.nodeid;
    FileContext->FileHandle = open_out.open.fh;
    
    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    SetFileInfo(&lookup_out.entry.attr, FileInfo);
    
    *PFileContext = FileContext;

    return Status;
}

static NTSTATUS Overwrite(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext0, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes,
    UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    NTSTATUS Status;

    DBG("FileAttributes: 0x%08x ReplaceFileAttributes: %d "
        "AllocationSize: %Iu", FileAttributes, ReplaceFileAttributes,
        AllocationSize);

    if ((FileAttributes != 0) && (FileAttributes != INVALID_FILE_ATTRIBUTES))
    {
        if (ReplaceFileAttributes == FALSE)
        {
            Status = GetFileInfoInternal(VirtFs, FileContext, FileInfo, NULL);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }

            FileAttributes |= FileInfo->FileAttributes;
        }

        if ((FileAttributes != FileInfo->FileAttributes))
        {
            Status = SetBasicInfo(FileSystem, FileContext0, FileAttributes,
                0LL, 0LL, 0LL, 0LL, FileInfo);

            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
        }
    }

    return SetFileSize(FileSystem, FileContext0, AllocationSize, FALSE,
        FileInfo);
}

static VOID Close(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    FUSE_RELEASE_IN release_in;
    FUSE_RELEASE_OUT release_out;

    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    FUSE_HEADER_INIT(&release_in.hdr,
        FileContext->IsDirectory ? FUSE_RELEASEDIR : FUSE_RELEASE,
        FileContext->NodeId, sizeof(release_in.release));

    release_in.release.fh = FileContext->FileHandle;
    release_in.release.flags = 0;
    release_in.release.lock_owner = 0;
    release_in.release.release_flags = 0;

    (VOID)VirtFsFuseRequest(VirtFs->Device, &release_in, sizeof(release_in),
        &release_out, sizeof(release_out));

    FspFileSystemDeleteDirectoryBuffer(&FileContext->DirBuffer);

    SafeHeapFree(FileContext);
}

static NTSTATUS Read(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    FUSE_READ_IN read_in;
    FUSE_READ_OUT *read_out;
    NTSTATUS Status;

    DBG("Offset: %Iu Length: %u", Offset, Length);
    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    *PBytesTransferred = 0;

    read_out = HeapAlloc(GetProcessHeap(), 0, sizeof(*read_out) + Length);
    if (read_out == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    read_in.read.fh = FileContext->FileHandle;
    read_in.read.offset = Offset;
    read_in.read.size = Length;
    read_in.read.read_flags = 0;
    read_in.read.lock_owner = 0;
    read_in.read.flags = 0;

    FUSE_HEADER_INIT(&read_in.hdr, FUSE_READ, FileContext->NodeId, Length);

    Status = VirtFsFuseRequest(VirtFs->Device, &read_in, sizeof(read_in),
        read_out, sizeof(*read_out) + Length);

    if (!NT_SUCCESS(Status))
    {
        SafeHeapFree(read_out);
        return Status;
    }

    *PBytesTransferred = read_out->hdr.len - sizeof(struct fuse_out_header);

    // A successful read with no bytes read mean file offset is at or past the
    // end of file.
    if (*PBytesTransferred == 0)
    {
        Status = STATUS_END_OF_FILE;
    }

    if (Buffer != NULL)
    {
        CopyMemory(Buffer, read_out->buf, *PBytesTransferred);
    }

    DBG("BytesTransferred: %d", *PBytesTransferred);

    SafeHeapFree(read_out);

    return Status;
}

static NTSTATUS Write(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile,
    BOOLEAN ConstrainedIo, PULONG PBytesTransferred,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    ULONG WriteSize;
    NTSTATUS Status;
    FUSE_WRITE_IN *write_in;
    FUSE_WRITE_OUT write_out;

    DBG("Buffer: %p Offset: %Iu Length: %u WriteToEndOfFile: %d "
        "ConstrainedIo: %d", Buffer, Offset, Length, WriteToEndOfFile,
        ConstrainedIo);
    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    // Both these cases requires knowing the actual file size.
    if ((WriteToEndOfFile == TRUE) || (ConstrainedIo == TRUE))
    {
        Status = GetFileInfoInternal(VirtFs, FileContext, FileInfo, NULL);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    if (WriteToEndOfFile == TRUE)
    {
        Offset = FileInfo->FileSize;
    }

    if (ConstrainedIo == TRUE)
    {
        if (Offset >= FileInfo->FileSize)
        {
            return STATUS_SUCCESS;
        }
        
        if ((Offset + Length) > FileInfo->FileSize)
        {
            Length = (ULONG)(FileInfo->FileSize - Offset);
        }
    }

    WriteSize = min(Length, VirtFs->MaxWrite);

    write_in = HeapAlloc(GetProcessHeap(), 0, sizeof(*write_in) + WriteSize);
    if (write_in == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    do
    {
        FUSE_HEADER_INIT(&write_in->hdr, FUSE_WRITE, FileContext->NodeId,
            sizeof(struct fuse_write_in) + WriteSize);

        write_in->write.fh = FileContext->FileHandle;
        write_in->write.offset = Offset + *PBytesTransferred;
        write_in->write.size = WriteSize;
        write_in->write.write_flags = 0;
        write_in->write.lock_owner = 0;
        write_in->write.flags = 0;

        CopyMemory(write_in->buf, (BYTE*)Buffer + *PBytesTransferred,
            WriteSize);

        Status = VirtFsFuseRequest(VirtFs->Device, write_in,
            write_in->hdr.len, &write_out, sizeof(write_out));

        if (!NT_SUCCESS(Status))
        {
            break;
        }

        *PBytesTransferred += write_out.write.size;
        Length -= write_out.write.size;
        WriteSize = min(Length, VirtFs->MaxWrite);
    }
    while (Length > 0);

    SafeHeapFree(write_in);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return GetFileInfoInternal(VirtFs, FileContext, FileInfo, NULL);
}

static NTSTATUS Flush(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    NTSTATUS Status;
    FUSE_FLUSH_IN flush_in;
    FUSE_FLUSH_OUT flush_out;

    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    FUSE_HEADER_INIT(&flush_in.hdr, FUSE_FLUSH, FileContext->NodeId,
        sizeof(flush_in.flush));

    flush_in.flush.fh = FileContext->FileHandle;
    flush_in.flush.unused = 0;
    flush_in.flush.padding = 0;
    flush_in.flush.lock_owner = 0;

    Status = VirtFsFuseRequest(VirtFs->Device, &flush_in, sizeof(flush_in),
        &flush_out, sizeof(flush_out));

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return GetFileInfoInternal(VirtFs, FileContext, FileInfo, NULL);
}

static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;

    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    return GetFileInfoInternal(VirtFs, FileContext, FileInfo, NULL);
}

static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime,
    UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO *FileInfo)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    NTSTATUS Status;
    FUSE_SETATTR_IN setattr_in;
    FUSE_SETATTR_OUT setattr_out;

    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    FUSE_HEADER_INIT(&setattr_in.hdr, FUSE_SETATTR, FileContext->NodeId,
        sizeof(setattr_in.setattr));

    ZeroMemory(&setattr_in.setattr, sizeof(setattr_in.setattr));
    
    if ((FileContext->IsDirectory == FALSE) &&
        (FileContext->FileHandle != INVALID_FILE_HANDLE))
    {
        setattr_in.setattr.valid |= FATTR_FH;
        setattr_in.setattr.fh = FileContext->FileHandle;
    }

    if (FileAttributes != INVALID_FILE_ATTRIBUTES)
    {
        setattr_in.setattr.valid |= FATTR_MODE;
        setattr_in.setattr.mode = 0664 /* -rw-rw-r-- */;

        if (!!(FileAttributes & FILE_ATTRIBUTE_READONLY) == TRUE)
        {
            setattr_in.setattr.mode &= ~0222;
        }

        if (!!(FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == TRUE)
        {
            setattr_in.setattr.mode |= 040111;
        }
    }

    if (LastAccessTime != 0)
    {
        setattr_in.setattr.valid |= FATTR_ATIME;
        FileTimeToUnixTime(LastAccessTime, &setattr_in.setattr.atime,
            &setattr_in.setattr.atimensec);
    }
    if ((LastWriteTime != 0) || (ChangeTime != 0))
    {
        if (LastWriteTime == 0)
        {
            LastWriteTime = ChangeTime;
        }
        setattr_in.setattr.valid |= FATTR_MTIME;
        FileTimeToUnixTime(LastWriteTime, &setattr_in.setattr.mtime,
            &setattr_in.setattr.mtimensec);
    }
    if (CreationTime != 0)
    {
        setattr_in.setattr.valid |= FATTR_CTIME;
        FileTimeToUnixTime(CreationTime, &setattr_in.setattr.ctime,
            &setattr_in.setattr.ctimensec);
    }

    Status = VirtFsFuseRequest(VirtFs->Device, &setattr_in,
        sizeof(setattr_in), &setattr_out, sizeof(setattr_out));

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return GetFileInfoInternal(VirtFs, FileContext, FileInfo, NULL);
}

static VOID Cleanup(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    PWSTR FileName, ULONG Flags)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    UINT64 LastAccessTime, LastWriteTime;
    FILETIME CurrentTime;
    NTSTATUS Status;
    char *filename, *fullpath;
    uint64_t parent;

    DBG("\"%S\" Flags: 0x%02x", FileName, Flags);

    if (FileName == NULL)
    {
        return;
    }

    Status = FspPosixMapWindowsToPosixPath(FileName + 1, &fullpath);
    if (!NT_SUCCESS(Status))
    {
        FspPosixDeletePath(fullpath);
        return;
    }

    Status = PathWalkthough(VirtFs->Device, fullpath, &filename, &parent);
    if (!NT_SUCCESS(Status))
    {
        FspPosixDeletePath(fullpath);
        return;
    }

    if (Flags & FspCleanupDelete)
    {
        SubmitDeleteRequest(VirtFs->Device, FileContext, filename, parent);
    }
    else
    {
        GetSystemTimeAsFileTime(&CurrentTime);
        LastAccessTime = LastWriteTime = 0;

        if (Flags & FspCleanupSetAllocationSize)
        {

        }
        if (Flags & FspCleanupSetArchiveBit)
        {

        }
        if (Flags & FspCleanupSetLastAccessTime)
        {
            LastAccessTime = ((PLARGE_INTEGER)&CurrentTime)->QuadPart;
        }
        if ((Flags & FspCleanupSetLastWriteTime) ||
            (Flags & FspCleanupSetChangeTime))
        {
            LastWriteTime = ((PLARGE_INTEGER)&CurrentTime)->QuadPart;
        }

        (VOID)SetBasicInfo(FileSystem, FileContext0, 0, 0, LastAccessTime,
            LastWriteTime, 0, NULL);
    }

    FspPosixDeletePath(fullpath);
}

static NTSTATUS SetFileSize(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    NTSTATUS Status;

    DBG("NewSize: %Iu SetAllocationSize: %d", NewSize, SetAllocationSize);
    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    if (SetAllocationSize == TRUE)
    {
        FUSE_FALLOCATE_IN falloc_in;
        FUSE_FALLOCATE_OUT falloc_out;

        FUSE_HEADER_INIT(&falloc_in.hdr, FUSE_FALLOCATE, FileContext->NodeId,
            sizeof(struct fuse_fallocate_in));

        falloc_in.hdr.uid = VirtFs->OwnerUid;
        falloc_in.hdr.gid = VirtFs->OwnerGid;

        falloc_in.falloc.fh = FileContext->FileHandle;
        falloc_in.falloc.offset = 0;
        falloc_in.falloc.length = NewSize;
        falloc_in.falloc.mode = 0x01; /* FALLOC_FL_KEEP_SIZE */
        falloc_in.falloc.padding = 0;

        Status = VirtFsFuseRequest(VirtFs->Device, &falloc_in, falloc_in.hdr.len,
            &falloc_out, sizeof(falloc_out));
    }
    else
    {
        FUSE_SETATTR_IN setattr_in;
        FUSE_SETATTR_OUT setattr_out;

        FUSE_HEADER_INIT(&setattr_in.hdr, FUSE_SETATTR, FileContext->NodeId,
            sizeof(setattr_in.setattr));

        ZeroMemory(&setattr_in.setattr, sizeof(setattr_in.setattr));
        setattr_in.setattr.valid = FATTR_SIZE;
        setattr_in.setattr.size = NewSize;

        Status = VirtFsFuseRequest(VirtFs->Device, &setattr_in,
            sizeof(setattr_in), &setattr_out, sizeof(setattr_out));
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return GetFileInfoInternal(VirtFs, FileContext, FileInfo, NULL);
}

static NTSTATUS CanDelete(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    PWSTR FileName)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    PVIRTFS_FILE_CONTEXT FileContext = FileContext0;
    FSP_FSCTL_FILE_INFO FileInfo;
    NTSTATUS Status;

    DBG("\"%S\"", FileName);
 
    Status = GetFileInfoInternal(VirtFs, FileContext, &FileInfo, NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    // Would be nice to know why the size of an empty directory is 40.
    if ((FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        (FileInfo.FileSize > 40))
    {
        return STATUS_DIRECTORY_NOT_EMPTY;
    }

    return Status;
}

static NTSTATUS Rename(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    FUSE_RENAME2_IN *rename2_in;
    FUSE_RENAME_OUT rename_out;
    NTSTATUS Status;
    char *oldname, *newname, *oldfullpath, *newfullpath;
    int oldname_size, newname_size;
    uint64_t oldparent, newparent;

    DBG("\"%S\" -> \"%S\" ReplaceIfExist: %d", FileName, NewFileName,
        ReplaceIfExists);
    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    Status = FspPosixMapWindowsToPosixPath(FileName + 1, &oldfullpath);
    if (!NT_SUCCESS(Status))
    {
        FspPosixDeletePath(oldfullpath);
        return Status;
    }

    Status = FspPosixMapWindowsToPosixPath(NewFileName + 1, &newfullpath);
    if (!NT_SUCCESS(Status))
    {
        FspPosixDeletePath(oldfullpath);
        FspPosixDeletePath(newfullpath);
        return Status;
    }

    Status = PathWalkthough(VirtFs->Device, oldfullpath, &oldname, &oldparent);
    if (!NT_SUCCESS(Status))
    {
        FspPosixDeletePath(oldfullpath);
        FspPosixDeletePath(newfullpath);
        return Status;
    }

    Status = PathWalkthough(VirtFs->Device, newfullpath, &newname, &newparent);
    if (!NT_SUCCESS(Status))
    {
        FspPosixDeletePath(oldfullpath);
        FspPosixDeletePath(newfullpath);
        return Status;
    }

    oldname_size = lstrlenA(oldname) + 1;
    newname_size = lstrlenA(newname) + 1;

    DBG("old: %s (%d) new: %s (%d)", oldname, oldname_size, newname,
        newname_size);

    rename2_in = HeapAlloc(GetProcessHeap(), 0, sizeof(*rename2_in) +
        oldname_size + newname_size);

    if (rename2_in == NULL)
    {
        FspPosixDeletePath(oldfullpath);
        FspPosixDeletePath(newfullpath);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    FUSE_HEADER_INIT(&rename2_in->hdr, FUSE_RENAME2, oldparent,
        sizeof(rename2_in->rename) + oldname_size + newname_size);

    rename2_in->rename.newdir = newparent;

    // It is not allowed to rename to an existing directory even when
    // ReplaceIfExists is set.
    rename2_in->rename.flags = ((FileContext->IsDirectory == FALSE) &&
        (ReplaceIfExists == TRUE)) ? 0 : (1 << 0) /* RENAME_NOREPLACE */;

    CopyMemory(rename2_in->names, oldname, oldname_size);
    CopyMemory(rename2_in->names + oldname_size, newname, newname_size);

    FspPosixDeletePath(oldfullpath);
    FspPosixDeletePath(newfullpath);

    Status = VirtFsFuseRequest(VirtFs->Device, rename2_in,
        rename2_in->hdr.len, &rename_out, sizeof(rename_out));

    // Fix to expected error when renaming a directory to existing directory.
    if ((FileContext->IsDirectory == TRUE) && (ReplaceIfExists == TRUE) &&
        (Status == STATUS_OBJECT_NAME_COLLISION))
    {
        Status = STATUS_ACCESS_DENIED;
    }

    return Status;
}

static NTSTATUS GetSecurity(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    PVIRTFS_FILE_CONTEXT FileContext = FileContext0;
    PSECURITY_DESCRIPTOR Security;
    DWORD SecurityLength;
    NTSTATUS Status;

    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    Status = GetFileInfoInternal(VirtFs, FileContext, NULL, &Security);
    if (!NT_SUCCESS(Status))
    {
        *PSecurityDescriptorSize = 0;
        return Status;
    }

    SecurityLength = GetSecurityDescriptorLength(Security);
    if (*PSecurityDescriptorSize < SecurityLength)
    {
        *PSecurityDescriptorSize = SecurityLength;
        SafeHeapFree(Security);
        return STATUS_BUFFER_TOO_SMALL;
    }

    *PSecurityDescriptorSize = SecurityLength;
    if (SecurityDescriptor != NULL)
    {
        CopyMemory(SecurityDescriptor, Security, SecurityLength);
    }

    SafeHeapFree(Security);

    return STATUS_SUCCESS;
}

static NTSTATUS SetSecurity(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    SECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR ModificationDescriptor)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    PVIRTFS_FILE_CONTEXT FileContext = FileContext0;
    PSECURITY_DESCRIPTOR FileSecurity, NewSecurityDescriptor;
    UINT32 Uid, Gid, Mode, NewMode;
    NTSTATUS Status;

    DBG("fh: %Iu nodeid: %Iu", FileContext->FileHandle, FileContext->NodeId);

    Status = GetFileInfoInternal(VirtFs, FileContext, NULL, &FileSecurity);
    if (!NT_SUCCESS(Status))
    {
        SafeHeapFree(FileSecurity);
        return Status;
    }

    Status = FspPosixMapSecurityDescriptorToPermissions(FileSecurity, &Uid,
        &Gid, &Mode);

    if (!NT_SUCCESS(Status))
    {
        SafeHeapFree(FileSecurity);
        return Status;
    }

    Status = FspSetSecurityDescriptor(FileSecurity, SecurityInformation,
        ModificationDescriptor, &NewSecurityDescriptor);

    if (!NT_SUCCESS(Status))
    {
        SafeHeapFree(FileSecurity);
        return Status;
    }

    SafeHeapFree(FileSecurity);

    Status = FspPosixMapSecurityDescriptorToPermissions(NewSecurityDescriptor,
        &Uid, &Gid, &NewMode);

    if (!NT_SUCCESS(Status))
    {
        FspDeleteSecurityDescriptor(NewSecurityDescriptor,
            (NTSTATUS(*)())FspSetSecurityDescriptor);
        return Status;
    }

    FspDeleteSecurityDescriptor(NewSecurityDescriptor,
        (NTSTATUS(*)())FspSetSecurityDescriptor);

    if (Mode != NewMode)
    {
        FUSE_SETATTR_IN setattr_in;
        FUSE_SETATTR_OUT setattr_out;

        FUSE_HEADER_INIT(&setattr_in.hdr, FUSE_SETATTR, FileContext->NodeId,
            sizeof(setattr_in.setattr));

        ZeroMemory(&setattr_in.setattr, sizeof(setattr_in.setattr));
        setattr_in.setattr.valid = FATTR_MODE;
        setattr_in.setattr.mode = NewMode;

        Status = VirtFsFuseRequest(VirtFs->Device, &setattr_in,
            sizeof(setattr_in), &setattr_out, sizeof(setattr_out));
    }

    return Status;
}

static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext0,
    PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG BufferLength,
    PULONG PBytesTransferred)
{
    VIRTFS *VirtFs = FileSystem->UserContext;
    VIRTFS_FILE_CONTEXT *FileContext = FileContext0;
    BYTE DirInfoBuf[sizeof(FSP_FSCTL_DIR_INFO) + MAX_PATH * sizeof(WCHAR)];
    FSP_FSCTL_DIR_INFO *DirInfo = (FSP_FSCTL_DIR_INFO *)DirInfoBuf;
    struct fuse_direntplus *DirEntryPlus;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT64 Offset = 0;
    UINT32 Remains;
    BOOLEAN Result;
    int FileNameLength;
    FUSE_READ_IN read_in;
    FUSE_READ_OUT *read_out;

    DBG("Pattern: %S Marker: %S BufferLength: %u",
        Pattern ? Pattern : TEXT("(null)"), Marker ? Marker : TEXT("(null)"),
        BufferLength);

    Result = FspFileSystemAcquireDirectoryBuffer(&FileContext->DirBuffer,
        Marker == NULL, &Status);

    if (Result == TRUE)
    {
        read_out = HeapAlloc(GetProcessHeap(), 0,
            sizeof(struct fuse_out_header) + BufferLength * 2);

        if (read_out != NULL)
        {
            for (;;)
            {
                FUSE_HEADER_INIT(&read_in.hdr, FUSE_READDIRPLUS,
                    FileContext->NodeId, sizeof(read_in.read));

                read_in.read.fh = FileContext->FileHandle;
                read_in.read.offset = Offset;
                read_in.read.size = BufferLength * 2;
                read_in.read.read_flags = 0;
                read_in.read.lock_owner = 0;
                read_in.read.flags = 0;

                Status = VirtFsFuseRequest(VirtFs->Device, &read_in,
                    sizeof(read_in), read_out,
                    sizeof(struct fuse_out_header) + read_in.read.size);

                if (!NT_SUCCESS(Status))
                {
                    break;
                }

                Remains = read_out->hdr.len - sizeof(struct fuse_out_header);
                if (Remains == 0)
                {
                    // A successful request with no data means no more
                    // entries.
                    break;
                }

                DirEntryPlus = (struct fuse_direntplus *)read_out->buf;

                while (Remains > sizeof(struct fuse_direntplus))
                {
                    DBG("ino=%Iu off=%Iu namelen=%u type=%u name=%s",
                        DirEntryPlus->dirent.ino, DirEntryPlus->dirent.off,
                        DirEntryPlus->dirent.namelen,
                        DirEntryPlus->dirent.type, DirEntryPlus->dirent.name);

                    ZeroMemory(DirInfoBuf, sizeof(DirInfoBuf));

                    // Not using FspPosixMapPosixToWindowsPath so we can do
                    // the conversion in-place.
                    FileNameLength = MultiByteToWideChar(CP_UTF8, 0,
                        DirEntryPlus->dirent.name,
                        DirEntryPlus->dirent.namelen, DirInfo->FileNameBuf,
                        MAX_PATH);

                    DBG("\"%S\" (%d)", DirInfo->FileNameBuf, FileNameLength);

                    if (FileNameLength > 0)
                    {
                        DirInfo->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) +
                            FileNameLength * sizeof(WCHAR));

                        SetFileInfo(&DirEntryPlus->entry_out.attr,
                            &DirInfo->FileInfo);

                        Result = FspFileSystemFillDirectoryBuffer(
                            &FileContext->DirBuffer, DirInfo, &Status);

                        if (Result == FALSE)
                        {
                            break;
                        }
                    }

                    Offset = DirEntryPlus->dirent.off;
                    Remains -= FUSE_DIRENTPLUS_SIZE(DirEntryPlus);
                    DirEntryPlus = (struct fuse_direntplus *)(
                        (PBYTE)DirEntryPlus +
                        FUSE_DIRENTPLUS_SIZE(DirEntryPlus));
                }
            }

            SafeHeapFree(read_out);
        }
        else
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }

        FspFileSystemReleaseDirectoryBuffer(&FileContext->DirBuffer);
    }

    if (NT_SUCCESS(Status))
    {
        FspFileSystemReadDirectoryBuffer(&FileContext->DirBuffer, Marker,
            Buffer, BufferLength, PBytesTransferred);
    }

    return Status;
}

static FSP_FILE_SYSTEM_INTERFACE VirtFsInterface =
{
    .GetVolumeInfo = GetVolumeInfo,
    .GetSecurityByName = GetSecurityByName,
    .Create = Create,
    .Open = Open,
    .Overwrite = Overwrite,
    .Cleanup = Cleanup,
    .Close = Close,
    .Read = Read,
    .Write = Write,
    .Flush = Flush,
    .GetFileInfo = GetFileInfo,
    .SetBasicInfo = SetBasicInfo,
    .SetFileSize = SetFileSize,
    .CanDelete = CanDelete,
    .Rename = Rename,
    .GetSecurity = GetSecurity,
    .SetSecurity = SetSecurity,
    .ReadDirectory = ReadDirectory
};

static ULONG wcstol_deflt(wchar_t *w, ULONG deflt)
{
    wchar_t *endp;
    ULONG ul = wcstol(w, &endp, 0);
    return L'\0' != w[0] && L'\0' == *endp ? ul : deflt;
}

static NTSTATUS SvcStart(FSP_SERVICE *Service, ULONG argc, PWSTR *argv)
{
#define argtos(v) if (arge > ++argp) v = *argp; else goto usage
#define argtol(v) if (arge > ++argp) v = wcstol_deflt(*argp, v); \
    else goto usage

    wchar_t **argp, **arge;
    WCHAR VolumePrefix[MAX_PATH];
    FSP_FSCTL_VOLUME_PARAMS VolumeParams;
    PWSTR DebugLogFile = 0;
    ULONG DebugFlags = 0;
    HANDLE DebugLogHandle = INVALID_HANDLE_VALUE;
    PWSTR MountPoint = NULL;
    VIRTFS *VirtFs;
    DWORD SessionId;
    FILETIME FileTime;
    NTSTATUS Status;
    FUSE_INIT_IN init_in;
    FUSE_INIT_OUT init_out;

    for (argp = argv + 1, arge = argv + argc; arge > argp; argp++)
    {
        if (L'-' != argp[0][0])
        {
            break;
        }

        switch (argp[0][1])
        {
            case L'?':
                goto usage;
            case L'd':
                argtol(DebugFlags);
                break;
            case L'D':
                argtos(DebugLogFile);
                break;
            default:
                goto usage;
        }
    }

    if (arge > argp)
    {
        goto usage;
    }

    if (DebugLogFile != 0)
    {
        if (0 == wcscmp(L"-", DebugLogFile))
        {
            DebugLogHandle = GetStdHandle(STD_ERROR_HANDLE);
        }
        else
        {
            DebugLogHandle = CreateFileW(DebugLogFile, FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, 0);
        }

        if (DebugLogHandle == INVALID_HANDLE_VALUE)
        {
            FspServiceLog(EVENTLOG_ERROR_TYPE,
                TEXT("Can not open debug log file."));
            goto usage;
        }

        FspDebugLogSetHandle(DebugLogHandle);
    }

    VirtFs = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*VirtFs));
    if (VirtFs == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = FindDeviceInterface(&VirtFs->Device);
    if (!NT_SUCCESS(Status))
    {
        VirtFsDelete(VirtFs);
        return Status;
    }

    lstrcpy(VolumePrefix, L"\\VirtioFS\\");
    GetVolumeName(VirtFs->Device, VolumePrefix + lstrlen(VolumePrefix),
        sizeof(VolumePrefix) - (lstrlen(VolumePrefix) * sizeof(WCHAR)));

    FUSE_HEADER_INIT(&init_in.hdr, FUSE_INIT, FUSE_ROOT_ID,
        sizeof(init_in.init));

    init_in.init.major = FUSE_KERNEL_VERSION;
    init_in.init.minor = FUSE_KERNEL_MINOR_VERSION;
    init_in.init.max_readahead = 0;
    init_in.init.flags = FUSE_DO_READDIRPLUS;

    Status = VirtFsFuseRequest(VirtFs->Device, &init_in, sizeof(init_in),
        &init_out, sizeof(init_out));

    if (!NT_SUCCESS(Status))
    {
        VirtFsDelete(VirtFs);
        return Status;
    }

    VirtFs->MaxWrite = init_out.init.max_write;

    SessionId = WTSGetActiveConsoleSessionId();
    if (SessionId != 0xFFFFFFFF)
    {
        UpdateLocalUidGid(VirtFs, SessionId);
    }

    GetSystemTimeAsFileTime(&FileTime);

    ZeroMemory(&VolumeParams, sizeof(VolumeParams));
    VolumeParams.Version = sizeof(FSP_FSCTL_VOLUME_PARAMS);
    VolumeParams.SectorSize = ALLOCATION_UNIT;
    VolumeParams.SectorsPerAllocationUnit = 1;
    VolumeParams.VolumeCreationTime = ((PLARGE_INTEGER)&FileTime)->QuadPart;
//    VolumeParams.VolumeSerialNumber = 0;
    VolumeParams.FileInfoTimeout = 1000;
    VolumeParams.CaseSensitiveSearch = 1;
    VolumeParams.CasePreservedNames = 1;
    VolumeParams.UnicodeOnDisk = 1;
    VolumeParams.PersistentAcls = 1;
    VolumeParams.PostCleanupWhenModifiedOnly = 1;
//    VolumeParams.PassQueryDirectoryPattern = 1;
    VolumeParams.FlushAndPurgeOnCleanup = 1;
    VolumeParams.UmFileContextIsUserContext2 = 1;
//    VolumeParams.DirectoryMarkerAsNextOffset = 1;
    if (VolumePrefix != NULL)
    {
        wcscpy_s(VolumeParams.Prefix,
            sizeof(VolumeParams.Prefix) / sizeof(WCHAR), VolumePrefix);
    }
    wcscpy_s(VolumeParams.FileSystemName,
        sizeof(VolumeParams.FileSystemName) / sizeof(WCHAR), FS_SERVICE_NAME);

    Status = FspFileSystemCreate(TEXT(FSP_FSCTL_NET_DEVICE_NAME),
        &VolumeParams, &VirtFsInterface, &VirtFs->FileSystem);

    if (!NT_SUCCESS(Status))
    {
        VirtFsDelete(VirtFs);
        return Status;
    }

    FspFileSystemSetDebugLog(VirtFs->FileSystem, DebugFlags);

    VirtFs->FileSystem->UserContext = Service->UserContext = VirtFs;

    Status = FspFileSystemSetMountPoint(VirtFs->FileSystem, MountPoint);
    if (NT_SUCCESS(Status))
    {
        Status = FspFileSystemStartDispatcher(VirtFs->FileSystem, 0);
    }

    if (!NT_SUCCESS(Status))
    {
        VirtFsDelete(VirtFs);
    }

    return Status;

usage:
    static wchar_t usage[] = L""
        "Usage: %s OPTIONS\n"
        "\n"
        "options:\n"
        "    -d DebugFlags       [-1: enable all debug logs]\n"
        "    -D DebugLogFile     [file path; use - for stderr]\n";

    FspServiceLog(EVENTLOG_ERROR_TYPE, usage, FS_SERVICE_NAME);

    return STATUS_UNSUCCESSFUL;

#undef argtos
#undef argtol
}

static NTSTATUS SvcStop(FSP_SERVICE *Service)
{
    VIRTFS *VirtFs = Service->UserContext;

    FspFileSystemStopDispatcher(VirtFs->FileSystem);
    VirtFsDelete(VirtFs);

    return STATUS_SUCCESS;
}

static NTSTATUS SvcControl(FSP_SERVICE *Service, ULONG Control,
    ULONG EventType, PVOID EventData)
{
    VIRTFS *VirtFs = Service->UserContext;
    PWTSSESSION_NOTIFICATION SessionNotification;

    switch (Control)
    {
        case SERVICE_CONTROL_DEVICEEVENT:
            break;

        case SERVICE_CONTROL_SESSIONCHANGE:
            SessionNotification = (PWTSSESSION_NOTIFICATION)EventData;
            if (EventType == WTS_SESSION_LOGON)
            {
                UpdateLocalUidGid(VirtFs, SessionNotification->dwSessionId);
            }
            break;

        default:
            break;
    }

    return STATUS_SUCCESS;
}

int wmain(int argc, wchar_t **argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    if (!NT_SUCCESS(FspLoad(0)))
    {
        return ERROR_DELAY_LOAD_FAILED;
    }

    return FspServiceRun(FS_SERVICE_NAME, SvcStart, SvcStop, SvcControl);
}
