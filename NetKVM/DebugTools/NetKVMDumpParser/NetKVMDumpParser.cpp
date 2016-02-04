/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: NetKVMDumpParser.cpp
 *
 * This file contains dump parsing logic
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "stdafx.h"
#include "NetKVMDumpParser.h"
#include "..\..\Common\DebugData.h"
#include <sal.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define PRINT_SEPARATOR "-------------------------------------"
#define DEBUG_SYMBOLS_IF    IDebugSymbols3

FILE *outf = stdout;

//#define UNDER_DEBUGGING

#ifndef UNDER_DEBUGGING
#define PRINT(fmt, ...) fprintf(outf, "[%s]: "##fmt##"\n", __FUNCTION__, __VA_ARGS__);
#else
#define PRINT(fmt, ...) { CString __s; __s.Format(TEXT("[%s]: ")##TEXT(fmt)##TEXT("\n"), TEXT(__FUNCTION__), __VA_ARGS__); OutputDebugString(__s.GetBuffer()); }
#endif

CString ErrorToString(HRESULT hr)
{
    CString s;
    LPTSTR lpMessageBuffer;
    if (FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER |
      FORMAT_MESSAGE_FROM_SYSTEM,
      NULL,
      hr,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), //The user default language
      (LPTSTR) &lpMessageBuffer,
      0,
      NULL ))
    {
      s = lpMessageBuffer;
      LocalFree(lpMessageBuffer);
    }
    else
    {
        s.Format("[Error %lu]", hr);
    }
    return s;
}

static const LPCSTR OpNames[] = {
    "PowerOff             ",
    "PowerOn              ",
    "SysPause             ",
    "SysResume            ",
    "InternalSendPause    ",
    "InternalReceivePause ",
    "InternalSendResume   ",
    "InternalReceiveResume",
    "SysReset             ",
    "Halt                 ",
    "ConnectIndication    ",
    "DPC                  ",
    "Send                 ",
    "SendNBLRequest       ",
    "SendPacketRequest    ",
    "SendPacketMapped     ",
    "SubmittedPacket      ",
    "BufferSent           ",
    "BufferReceivedStat   ",
    "BufferReturned       ",
    "SendComplete         ",
    "TxProcess            ",
    "PacketReceived       ",
    "OidRequest           ",
    "PnpEvent             ",
};

static CString HistoryOperationName(ULONG op)
{
    CString s;
    if (op < sizeof(OpNames)/ sizeof(OpNames[0]))
        s = OpNames[op];
    else
        s.Format("##%d", op);
    return s;
}

typedef struct _tagNamedFlag
{
    ULONG64 flag;
    LPCSTR  name;
}tNamedFlag;

typedef struct _tagNamedValue
{
    ULONG64 value;
    LPCSTR  name;
}tNamedValue;

#define STRINGER(x)     #x
#define VALUE(v) { v, STRINGER(##v) }
#define ENDTABLE    { 0, NULL }

const tNamedValue SessionStatusValues[] =
{
VALUE(DEBUG_SESSION_ACTIVE),
VALUE(DEBUG_SESSION_END_SESSION_ACTIVE_TERMINATE),
VALUE(DEBUG_SESSION_END_SESSION_ACTIVE_DETACH),
VALUE(DEBUG_SESSION_END_SESSION_PASSIVE),
VALUE(DEBUG_SESSION_END),
VALUE(DEBUG_SESSION_REBOOT),
VALUE(DEBUG_SESSION_HIBERNATE),
VALUE(DEBUG_SESSION_FAILURE),
ENDTABLE
};

const tNamedValue DebuggeeStateValues[] =
{
VALUE(DEBUG_CDS_ALL),
VALUE(DEBUG_CDS_REGISTERS),
VALUE(DEBUG_CDS_DATA),
ENDTABLE
};

const tNamedValue DebuggeeStateArgValues[] =
{
VALUE(DEBUG_DATA_SPACE_VIRTUAL),
VALUE(DEBUG_DATA_SPACE_PHYSICAL),
VALUE(DEBUG_DATA_SPACE_CONTROL),
VALUE(DEBUG_DATA_SPACE_IO),
VALUE(DEBUG_DATA_SPACE_MSR),
VALUE(DEBUG_DATA_SPACE_BUS_DATA),
VALUE(DEBUG_DATA_SPACE_DEBUGGER_DATA),
VALUE(DEBUG_DATA_SPACE_COUNT),
ENDTABLE
};

const tNamedFlag EngineStateFlags[] =
{
VALUE(DEBUG_CES_ALL),
VALUE(DEBUG_CES_CURRENT_THREAD),
VALUE(DEBUG_CES_EFFECTIVE_PROCESSOR),
VALUE(DEBUG_CES_BREAKPOINTS),
VALUE(DEBUG_CES_CODE_LEVEL),
VALUE(DEBUG_CES_EXECUTION_STATUS),
VALUE(DEBUG_CES_ENGINE_OPTIONS),
VALUE(DEBUG_CES_LOG_FILE),
VALUE(DEBUG_CES_RADIX),
VALUE(DEBUG_CES_EVENT_FILTERS),
VALUE(DEBUG_CES_PROCESS_OPTIONS),
VALUE(DEBUG_CES_EXTENSIONS),
VALUE(DEBUG_CES_SYSTEMS),
VALUE(DEBUG_CES_ASSEMBLY_OPTIONS),
VALUE(DEBUG_CES_EXPRESSION_SYNTAX),
VALUE(DEBUG_CES_TEXT_REPLACEMENTS),
ENDTABLE
};


const tNamedFlag SymbolStateFlags[] =
{
VALUE(DEBUG_CSS_ALL),
VALUE(DEBUG_CSS_LOADS),
VALUE(DEBUG_CSS_UNLOADS),
VALUE(DEBUG_CSS_SCOPE),
VALUE(DEBUG_CSS_PATHS),
VALUE(DEBUG_CSS_SYMBOL_OPTIONS),
VALUE(DEBUG_CSS_TYPE_OPTIONS),
ENDTABLE
};

#if !defined(DEBUG_STATUS_RESTART_REQUESTED)
#error Path to debug SDK is not defined properly in Property Manager.
#endif


const tNamedValue EngineExecutionStatus[] =
{
VALUE(DEBUG_STATUS_NO_CHANGE),
VALUE(DEBUG_STATUS_GO),
VALUE(DEBUG_STATUS_GO_HANDLED),
VALUE(DEBUG_STATUS_GO_NOT_HANDLED),
VALUE(DEBUG_STATUS_STEP_OVER),
VALUE(DEBUG_STATUS_STEP_INTO),
VALUE(DEBUG_STATUS_BREAK),
VALUE(DEBUG_STATUS_NO_DEBUGGEE),
VALUE(DEBUG_STATUS_STEP_BRANCH),
VALUE(DEBUG_STATUS_IGNORE_EVENT),
VALUE(DEBUG_STATUS_RESTART_REQUESTED),
VALUE(DEBUG_STATUS_REVERSE_GO),
VALUE(DEBUG_STATUS_REVERSE_STEP_BRANCH),
VALUE(DEBUG_STATUS_REVERSE_STEP_OVER),
VALUE(DEBUG_STATUS_REVERSE_STEP_INTO),
ENDTABLE
};

const tNamedFlag EngineExecutionStatusFlags[] =
{
VALUE(DEBUG_STATUS_INSIDE_WAIT),
VALUE(DEBUG_STATUS_WAIT_TIMEOUT),
ENDTABLE
};

const tNamedFlag ModuleFlags[] =
{
VALUE(DEBUG_MODULE_LOADED),
VALUE(DEBUG_MODULE_UNLOADED),
VALUE(DEBUG_MODULE_USER_MODE),
VALUE(DEBUG_MODULE_EXPLICIT),
VALUE(DEBUG_MODULE_SECONDARY),
VALUE(DEBUG_MODULE_SYNTHETIC),
VALUE(DEBUG_MODULE_SYM_BAD_CHECKSUM),
ENDTABLE
};

const tNamedValue SymbolTypeValues[] =
{
VALUE(DEBUG_SYMTYPE_NONE),
VALUE(DEBUG_SYMTYPE_COFF),
VALUE(DEBUG_SYMTYPE_CODEVIEW),
VALUE(DEBUG_SYMTYPE_PDB),
VALUE(DEBUG_SYMTYPE_EXPORT),
VALUE(DEBUG_SYMTYPE_DEFERRED),
VALUE(DEBUG_SYMTYPE_SYM),
VALUE(DEBUG_SYMTYPE_DIA),
ENDTABLE
};


const tNamedFlag SymbolOptionsFlags[] =
{
VALUE(SYMOPT_CASE_INSENSITIVE),
VALUE(SYMOPT_UNDNAME),
VALUE(SYMOPT_DEFERRED_LOADS),
VALUE(SYMOPT_NO_CPP),
VALUE(SYMOPT_LOAD_LINES),
VALUE(SYMOPT_OMAP_FIND_NEAREST),
VALUE(SYMOPT_LOAD_ANYTHING),
VALUE(SYMOPT_IGNORE_CVREC),
VALUE(SYMOPT_NO_UNQUALIFIED_LOADS),
VALUE(SYMOPT_FAIL_CRITICAL_ERRORS),
VALUE(SYMOPT_EXACT_SYMBOLS),
VALUE(SYMOPT_ALLOW_ABSOLUTE_SYMBOLS),
VALUE(SYMOPT_IGNORE_NT_SYMPATH),
VALUE(SYMOPT_INCLUDE_32BIT_MODULES),
VALUE(SYMOPT_PUBLICS_ONLY),
VALUE(SYMOPT_NO_PUBLICS),
VALUE(SYMOPT_AUTO_PUBLICS),
VALUE(SYMOPT_NO_IMAGE_SEARCH),
VALUE(SYMOPT_SECURE),
VALUE(SYMOPT_NO_PROMPTS),
VALUE(SYMOPT_OVERWRITE),
VALUE(SYMOPT_IGNORE_IMAGEDIR),
VALUE(SYMOPT_FLAT_DIRECTORY),
VALUE(SYMOPT_FAVOR_COMPRESSED),
VALUE(SYMOPT_ALLOW_ZERO_ADDRESS),
VALUE(SYMOPT_DISABLE_SYMSRV_AUTODETECT),
VALUE(SYMOPT_DEBUG),
ENDTABLE
};

typedef struct _tagModule
{
    LPCSTR  name;
    ULONG64 Base;
    ULONG   index;
}tModule;

class tDumpParser : public DebugBaseEventCallbacks, public IDebugOutputCallbacks
{
public:
    tDumpParser() : refCount(0), Client(NULL), Control(NULL), DataSpaces(NULL), DebugSymbols(NULL)
    {
        HRESULT hr;
        bEnableOutput = FALSE;
        hr = DebugCreate(__uuidof(IDebugClient), (void **)&Client);
        if (Client) hr = Client->QueryInterface(__uuidof(IDebugControl2), (void **)&Control);
        if (Client) hr = Client->QueryInterface(__uuidof(IDebugDataSpaces3), (void **)&DataSpaces);
        if (Client) hr = Client->QueryInterface(__uuidof(DEBUG_SYMBOLS_IF), (void **)&DebugSymbols);
    }
    ~tDumpParser()
    {
        ULONG ul = 0;
        if (DebugSymbols) DebugSymbols->Release();
        if (Control) Control->Release();
        if (DataSpaces) DataSpaces->Release();
        if (Client) ul = Client->Release();
        PRINT("finished (%d)", ul);
    }
    BOOL LoadFile(TCHAR *filename)
    {
        HRESULT hr = S_FALSE;
        if (Client && Control && DataSpaces)
        {
            hr = Client->SetEventCallbacks(this);
            if (S_OK == hr) hr = Client->SetOutputCallbacks(this);
            DebugSymbols->AddSymbolOptions(SYMOPT_DEBUG);
            //DebugSymbols->RemoveSymbolOptions(SYMOPT_DEFERRED_LOADS);
            DebugSymbols->AppendSymbolPath(".");
        }
        else
        {
            CString sMessage = TEXT("Error: Not all required interaces are up\n");
            if (!Client) sMessage += TEXT("Client interface is not initialized\n");
            if (!Control) sMessage += TEXT("Control interface is not initialized\n");
            if (!DataSpaces) sMessage += TEXT("Data interface is not initialized\n");
            PRINT("%s", sMessage.GetBuffer());
        }
        if (hr == S_OK)
        {
            hr = Client->OpenDumpFile(filename);
        }
        if (hr == S_OK) Control->WaitForEvent(0, INFINITE);
        return hr == S_OK;
    }
    void ProcessDumpFile();
    void FindOurTaggedCrashData(BOOL bWithSymbols);
    BOOL CheckLoadedSymbols(tModule *pModule);
    void ProcessSymbols(tModule *pModule);
    void ParseCrashData(tBugCheckStaticDataHeader *ph, ULONG64 databuffer, ULONG bytesRead, BOOL bWithSymbols);
    typedef enum _tageSystemProperty {
        espSymbolPath,
        espSystemVersion,
        espSystemTime,
    } eSystemProperty;
    CString GetProperty(eSystemProperty Prop);
protected:
    CString Parse(ULONG64 val, const tNamedValue *pt)
    {
        CString s;
        while (pt->name) { if (pt->value == val) { s = pt->name; break; } pt++; }
        if (s.IsEmpty()) s.Format(TEXT("Unknown value 0x%I64X"), val);
        return s;
    }

    CString Parse(ULONG64 val, const tNamedFlag *pt)
    {
        CString s;
        while (pt->name && val)
        {
            if ((pt->flag & val) == pt->flag)
            {
                val &= ~pt->flag;
                if (!s.IsEmpty()) s += ' ';
                s += pt->name;
            }
            pt++;
        }
        if (val)
        {
            CString sv;
            sv.Format(TEXT("0x%X"), val);
            if (!s.IsEmpty()) s += ' ';
            s += sv;
        }
        return s;
    }
protected:
    STDMETHOD_(ULONG, AddRef)(
        THIS
        ) { return ++refCount; }
    STDMETHOD_(ULONG, Release)(
        THIS
        ) { return --refCount; }
   STDMETHOD(QueryInterface)(
        THIS_
        __in REFIID InterfaceId,
        __out PVOID* Interface
        )
    {
        *Interface = NULL;
        if (IsEqualIID(InterfaceId, __uuidof(IUnknown)) ||
            IsEqualIID(InterfaceId, __uuidof(IDebugEventCallbacks)))
        {
            *Interface = (IDebugEventCallbacks *)this;
        }
        else if (IsEqualIID(InterfaceId, __uuidof(IDebugOutputCallbacks)))
        {
            *Interface = (IDebugOutputCallbacks *)this;
        }
        if (*Interface) AddRef();
        return (*Interface) ? S_OK : E_NOINTERFACE;
   }
    STDMETHOD(GetInterestMask)(
        THIS_
        __out PULONG Mask
        )
    {
        *Mask =
DEBUG_EVENT_BREAKPOINT              |
DEBUG_EVENT_EXCEPTION               |
DEBUG_EVENT_LOAD_MODULE             |
DEBUG_EVENT_UNLOAD_MODULE           |
DEBUG_EVENT_SYSTEM_ERROR            |
DEBUG_EVENT_SESSION_STATUS          |
DEBUG_EVENT_CHANGE_DEBUGGEE_STATE   |
DEBUG_EVENT_CHANGE_ENGINE_STATE     |
DEBUG_EVENT_CHANGE_SYMBOL_STATE     ;
        return S_OK;
    }
    STDMETHOD(Breakpoint)(
        THIS_
        __in PDEBUG_BREAKPOINT Bp
        )
    {
        PRINT("");
        return DEBUG_STATUS_BREAK;
    }
    STDMETHOD(Exception)(
        THIS_
        __in PEXCEPTION_RECORD64 Exception,
        __in ULONG FirstChance
        )
    {
        PRINT("");
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(LoadModule)(
        THIS_
        __in ULONG64 ImageFileHandle,
        __in ULONG64 BaseOffset,
        __in ULONG ModuleSize,
        __in PCSTR ModuleName,
        __in PCSTR ImageName,
        __in ULONG CheckSum,
        __in ULONG TimeDateStamp
        )
    {
        PRINT("%s", ImageName);
        return DEBUG_STATUS_NO_CHANGE;
    }
    STDMETHOD(UnloadModule)(
        THIS_
        __in PCSTR ImageBaseName,
        __in ULONG64 BaseOffset
        )
    {
        PRINT("%s", ImageBaseName);
        return DEBUG_STATUS_NO_CHANGE;
    }
    STDMETHOD(SystemError)(
        THIS_
        __in ULONG Error,
        __in ULONG Level
        )
    {
        PRINT("%s");
        return DEBUG_STATUS_NO_CHANGE;
    }
    STDMETHOD(SessionStatus)(
        THIS_
        __in ULONG Status
        )
    {
        CString s = Parse(Status, SessionStatusValues);
        PRINT("%s", s.GetBuffer());
        return DEBUG_STATUS_NO_CHANGE;
    }
    STDMETHOD(ChangeDebuggeeState)(
        THIS_
        __in ULONG Flags,
        __in ULONG64 Argument
        )
    {
        CString sf = Parse(Flags, DebuggeeStateValues);
        CString sarg;
        if (Flags == DEBUG_CDS_DATA) sarg = Parse(Argument, DebuggeeStateArgValues);
        else sarg.Format(TEXT("0x%I64X"), Argument);
        PRINT("%s(%s)", sf.GetBuffer(), sarg.GetBuffer());
        return DEBUG_STATUS_NO_CHANGE;
        return S_OK;
    }
    STDMETHOD(ChangeEngineState)(
        THIS_
        __in ULONG Flags,
        __in ULONG64 Argument
        )
    {
        CString s = Parse(Flags, EngineStateFlags);
        CString sArg;
        if (Flags == DEBUG_CES_EXECUTION_STATUS)
        {
            CString sWait = Parse(Argument & ~DEBUG_STATUS_MASK, EngineExecutionStatusFlags);
            sArg = Parse(Argument & DEBUG_STATUS_MASK, EngineExecutionStatus);
            sArg += ' ';
            sArg += sWait;
        }
        else
            sArg.Format(TEXT("arg 0x%I64X"), Argument);
        PRINT("%s(%s)", s.GetBuffer(), sArg.GetBuffer());
        if (Flags == DEBUG_CES_EXECUTION_STATUS && (Argument & DEBUG_STATUS_MASK) == DEBUG_STATUS_BREAK
            && !(Argument & ~DEBUG_STATUS_MASK))
        {
            ProcessDumpFile();
        }
        return S_OK;
    }
    STDMETHOD(ChangeSymbolState)(
        THIS_
        __in ULONG Flags,
        __in ULONG64 Argument
        )
    {
        CString s = Parse(Flags, SymbolStateFlags);
        PRINT("%s(arg 0x%I64X)", s.GetBuffer(), Argument);
        return S_OK;
    }
   // IDebugOutputCallbacks.
    STDMETHOD(Output)(
        THIS_
        __in ULONG Mask,
        __in PCSTR Text
        )
    {
        if (bEnableOutput)
        {
            PRINT("%s", Text);
        }
        return S_OK;
    }
    ULONG refCount;
    IDebugClient  *Client;
    IDebugControl2 *Control;
    IDebugDataSpaces3 *DataSpaces;
    DEBUG_SYMBOLS_IF *DebugSymbols;
    BOOL bEnableOutput;
};

#define CHECK_INTERFACE(xf) { \
xf *p; HRESULT hr = Client->QueryInterface(__uuidof(xf), (void **)&p); \
PRINT("Interface " TEXT(STRINGER(xf)) TEXT(" %spresent"), p ? "" : "NOT "); \
if (p) p->Release(); }


static void ParseHistoryEntry(LONGLONG basetime, tBugCheckHistoryDataEntry *phist, LONG Index)
{
    if (!phist)
    {
        PRINT("Op                    Ctx           Time    Params");
    }
    else if (phist[Index].Context)
    {
        LONGLONG diffInt = (basetime - phist[Index].TimeStamp.QuadPart) / 10;
        CString sOp = HistoryOperationName(phist[Index].operation);

#if (PARANDIS_DEBUG_HISTORY_DATA_VERSION == 0)
        PRINT("%s %I64X [-%09I64d] x%08X x%08X x%08X %I64X", sOp.GetBuffer(), phist[Index].Context, diffInt, phist[Index].lParam2, phist[Index].lParam3, phist[Index].lParam4, phist[Index].pParam1 );
#elif (PARANDIS_DEBUG_HISTORY_DATA_VERSION == 1)
        PRINT("CPU[%d] IRQL[%d] %s %I64X [-%09I64d] x%08X x%08X x%08X %I64X", phist[Index].uProcessor, phist[Index].uIRQL, sOp.GetBuffer(), phist[Index].Context, diffInt, phist[Index].lParam2, phist[Index].lParam3, phist[Index].lParam4, phist[Index].pParam1 );
#endif
    }
}

void tDumpParser::ParseCrashData(tBugCheckStaticDataHeader *ph, ULONG64 databuffer, ULONG bytesRead, BOOL bWithSymbols)
{
    UINT i;
    for (i = 0; i < ph->ulMaxContexts; ++i)
    {
        tBugCheckPerNicDataContent_V0 *pndc = (tBugCheckPerNicDataContent_V0 *)(ph->PerNicData - databuffer + (PUCHAR)ph);
        pndc += i;
        if (pndc->Context)
        {
            LONGLONG diffInt = (ph->qCrashTime.QuadPart - pndc->LastInterruptTimeStamp.QuadPart) / 10;
            LONGLONG diffTx  = (ph->qCrashTime.QuadPart - pndc->LastTxCompletionTimeStamp.QuadPart) / 10;
            PRINT(PRINT_SEPARATOR);
            PRINT("Context %I64X:", pndc->Context);
            PRINT("\tLastInterrupt %I64d us before crash", diffInt);
            PRINT("\tLast Tx complete %I64d us before crash", diffTx);
            PRINT("\tWaiting %d packets, %d free buffers", pndc->nofPacketsToComplete, pndc->nofReadyTxBuffers);
            PRINT(PRINT_SEPARATOR);
        }
    }
    tBugCheckStaticDataContent_V0 *pd = (tBugCheckStaticDataContent_V0 *)(ph->DataArea - databuffer + (PUCHAR)ph);
    tBugCheckHistoryDataEntry *phist = (tBugCheckHistoryDataEntry *)(pd->HistoryData - databuffer + (PUCHAR)ph);
    PRINT(PRINT_SEPARATOR);
    if (pd->SizeOfHistory > 2)
    {
        PRINT("History: version %d, %d entries of %d, current at %d", pd->HistoryDataVersion, pd->SizeOfHistory,  pd->SizeOfHistoryEntry, pd->CurrentHistoryIndex);
        LONG Index = pd->CurrentHistoryIndex % pd->SizeOfHistory;
        LONG EndIndex = Index;
        ParseHistoryEntry(NULL, NULL, 0);
        for (; Index < (LONG)pd->SizeOfHistory; Index++)
        {
            ParseHistoryEntry(ph->qCrashTime.QuadPart, phist, Index);
        }
        for (Index = 0; Index < EndIndex; Index++)
        {
            ParseHistoryEntry(ph->qCrashTime.QuadPart, phist, Index);
        }
    }
    else
    {
        PRINT("History records are not available");
    }
    PRINT(PRINT_SEPARATOR);
}



void tDumpParser::FindOurTaggedCrashData(BOOL bWithSymbols)
{
    ULONG64 h;
    if (S_OK == DataSpaces->StartEnumTagged(&h))
    {
        UCHAR ourBuffer[16];
        ULONG size;
        GUID  guid;
        while (S_OK == DataSpaces->GetNextTagged(h, &guid, &size))
        {
            WCHAR string[64];
            StringFromGUID2(guid, string, sizeof(string)/sizeof(string[0]));
            //PRINT("Found %S(size %d)", string, size);
            if (IsEqualGUID(ParaNdis_CrashGuid, guid))
            {
                PRINT("Found NetKVM GUID");
                if (S_OK == DataSpaces->ReadTagged(&guid,  0, ourBuffer, size, &size))
                {
                    if (size >= sizeof(tBugCheckDataLocation))
                    {
                        tBugCheckDataLocation *bcdl = (tBugCheckDataLocation *)ourBuffer;
                        PRINT("Found NetKVM data at %I64X, size %d", bcdl->Address, bcdl->Size);
                        ULONG bufferSize= (ULONG)bcdl->Size;
                        ULONG bytesRead;
                        PVOID databuffer = malloc(bufferSize);
                        if (databuffer)
                        {
                            if (S_OK == DataSpaces->ReadVirtual(bcdl->Address, databuffer, bufferSize, &bytesRead))
                            {
                                tBugCheckStaticDataHeader *ph = (tBugCheckStaticDataHeader *)databuffer;
                                PRINT("Retrieved %d bytes of data", bytesRead);
                                if (bytesRead >= sizeof(tBugCheckStaticDataHeader))
                                {
                                    PRINT("Versions: status data %d, pre-NIC data %d, ptr size %d, %d contexts, crash time %I64X",
                                        ph->StaticDataVersion, ph->PerNicDataVersion, ph->SizeOfPointer, ph->ulMaxContexts, ph->qCrashTime);
                                    PRINT("Per-NIC data at %I64X, Static data at %I64X(%d bytes)", ph->PerNicData, ph->DataArea, ph->DataAreaSize);
                                    ParseCrashData(ph, bcdl->Address, bytesRead, bWithSymbols);
                                }
                            }
                            free(databuffer);
                        }
                    }
                }
                break;
            }
        }
        DataSpaces->EndEnumTagged(h);

    }
}

static BOOL GetModuleName(ULONG Which, ULONG64 Base, DEBUG_SYMBOLS_IF *DebugSymbols, CString& s)
{
    HRESULT hr;
    ULONG bufsize = 1024;
    char *buf = (char *)malloc(bufsize);
    *buf = 0;
    hr = DebugSymbols->GetModuleNameString(
        Which,
        DEBUG_ANY_ID,
        Base,
        buf,
        bufsize,
        NULL);
    s = buf;
    free(buf);
    return S_OK == hr;
}

static BOOL TryMatchingSymbols(DEBUG_SYMBOLS_IF *DebugSymbols, PCSTR name, BOOL bPrintAll = FALSE)
{
    UINT n = 0;
    CString s;
    s.Format("%s!*", name);
    ULONG64 matchHandle;
    if (S_OK == DebugSymbols->StartSymbolMatch(s.GetBuffer(), &matchHandle))
    {
        ULONG size = 1024;
        char *buf = (char *)malloc(size);
        *buf = 0;
        while (S_OK == DebugSymbols->GetNextSymbolMatch(
                matchHandle, buf, size, NULL, NULL))
        {
            n++;
            if (!bPrintAll) break;
            PRINT("%s", buf);
        }
        DebugSymbols->EndSymbolMatch(matchHandle);
        free(buf);
    }
    return n != 0;
}

BOOL tDumpParser::CheckLoadedSymbols(tModule *pModule)
{
    BOOL bLoaded = FALSE;
    ULONG bufferSize = 2048, bufferUsage;
    UNREFERENCED_PARAMETER(bufferUsage);
    char *buffer = (char *)malloc(bufferSize);
    DEBUG_MODULE_PARAMETERS ModuleParams;
    HRESULT hr = DebugSymbols->GetModuleByModuleName(pModule->name, 0, &pModule->index, &pModule->Base);
    if (S_OK == hr)
    {
        CString s;
        PRINT("Found %s at %I64X", pModule->name, pModule->Base);
        if (GetModuleName(DEBUG_MODNAME_MODULE, pModule->Base, DebugSymbols, s))
            PRINT("\tModule Name:%s", s.GetBuffer());
        if (GetModuleName(DEBUG_MODNAME_IMAGE, pModule->Base, DebugSymbols, s))
            PRINT("\tImage Name:%s", s.GetBuffer());
        if (GetModuleName(DEBUG_MODNAME_SYMBOL_FILE, pModule->Base, DebugSymbols, s))
            PRINT("\tSymbol file:%s", s.GetBuffer());

        bLoaded = 0 != TryMatchingSymbols(DebugSymbols, pModule->name);
        PRINT("Symbols for %s %sLOADED", pModule->name, bLoaded ? "" : "NOT ");

        if (S_OK == DebugSymbols->GetModuleParameters(1, &pModule->Base, 0, &ModuleParams))
        {
            CString sSymbolType = Parse(ModuleParams.SymbolType, SymbolTypeValues);
            CString sFlags = Parse(ModuleParams.Flags, ModuleFlags);
            PRINT("Symbol Type %s, Flags %s", sSymbolType.GetBuffer(), sFlags.GetBuffer());
            // timestamp from 1.1.1970
            __time32_t timestamp = ModuleParams.TimeDateStamp;
            tm *timer = _gmtime32(&timestamp);
            strftime(buffer, bufferSize, "%d %b %Y %H:%M:%S UTC", timer);
            //PRINT("Checksum: %X", ModuleParams.Checksum);
            PRINT("Time Stamp: %s(%X)", buffer, ModuleParams.TimeDateStamp);
        }
    }
    free(buffer);
    return bLoaded;
}

void tDumpParser::ProcessDumpFile()
{
    ULONG SymbolOptions;
    BOOL  bSymbols;
    CString s;
    tModule module;
    module.name = "netkvm";
    if (S_OK == DebugSymbols->GetSymbolOptions(&SymbolOptions))
    {
        s = Parse(SymbolOptions, SymbolOptionsFlags);
        PRINT("Symbol options %s", s.GetBuffer());
    }

    s = GetProperty(espSymbolPath);
    PRINT("Symbol path:%s", s.GetBuffer());
    s = GetProperty(espSystemVersion);
    PRINT("System version:%s", s.GetBuffer());
    s = GetProperty(espSystemTime);
    PRINT("Crash time:%s", s.GetBuffer());
    bSymbols = CheckLoadedSymbols(&module);
    FindOurTaggedCrashData(bSymbols);
}

CString tDumpParser::GetProperty(eSystemProperty Prop)
{
    CString s;
    switch (Prop)
    {
        case espSymbolPath:
            if (DebugSymbols)
            {
                ULONG maxlen = 1024, len;
                char *buf = (char *)malloc(maxlen);
                *buf = 0;
                DebugSymbols->GetSymbolPath(buf, maxlen, &len);
                s = buf;
                free(buf);
            }
            break;
        case espSystemVersion:
            if (Control)
            {
                ULONG platform, major, minor, sp, buildsize = 128, buildused, procs = 0;
                char *buf = (char *)malloc(buildsize);
                *buf = 0;
                BOOL Is64 = Control->IsPointer64Bit() == S_OK;
                CString sSP;
                Control->GetNumberProcessors(&procs);
                Control->GetSystemVersion(&platform, &major, &minor, NULL, 0, NULL, &sp, buf, buildsize, &buildused);
                if (sp) sSP.Format("(SP%d.%d)", sp >> 8, sp & 0xff);
                s.Format("(%X)%d%s%s,%s,%d CPU",
                    major, minor, sSP.GetBuffer(), Is64 ? "(64-bit)" : "", buf, procs);
                free(buf);
            }
            break;
        case espSystemTime:
            if (Control)
            {
                ULONG ulSecondSince1970 = 0, ulUpTime = 0;
                Control->GetCurrentTimeDate(&ulSecondSince1970);
                Control->GetCurrentSystemUpTime(&ulUpTime);
                s = "Unknown";
                if (ulSecondSince1970)
                {
                    char buffer[256] = {0};
                    ULONG days, hours, min, sec, rem;
                    days = ulUpTime / (60*60*24);
                    rem = ulUpTime - days * 60*60*24;
                    hours = rem / (60*60);
                    rem = rem - hours * 60 * 60;
                    min = rem / 60;
                    rem = rem - min * 60;
                    sec = rem;
                    __time32_t timestamp = ulSecondSince1970;
                    tm *timer = _localtime32(&timestamp);
                    strftime(buffer, sizeof(buffer), "%b %d %H:%M:%S %Y(Local)", timer);
                    s.Format("%s (Up time %d:%d:%d:%d)", buffer, days, hours, min, sec);
                }
            }
            break;
        default:
            break;
    }
    return s;
}

int ParseDumpFile(int argc, TCHAR* argv[])
{
    if (argc == 2)
    {
        CString s = argv[1];
        s += ".txt";
        FILE *f = fopen(s.GetBuffer(), "w+t");
        if (f) outf = f;
        tDumpParser Parser;
#ifdef UNDER_DEBUGGING
        fputs("UNDER_DEBUGGING, the output is redirected to debugger", f);
#endif
        if (!Parser.LoadFile(argv[1])) PRINT("Failed to load dump file %s", argv[1]);
        if (f) fclose(f);
    }
    else
    {
        PRINT("%s", SessionStatusValues[0].name);
    }
    return 0;
}
