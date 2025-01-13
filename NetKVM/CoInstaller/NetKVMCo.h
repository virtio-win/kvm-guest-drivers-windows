#pragma once

typedef DWORD(WINAPI NS_HELPER_START_FN)(_In_ CONST GUID *pguidParent, _In_ DWORD dwVersion);

typedef NS_HELPER_START_FN *PNS_HELPER_START_FN;

typedef DWORD(WINAPI NS_HELPER_STOP_FN)(_In_ DWORD dwReserved);

typedef NS_HELPER_STOP_FN *PNS_HELPER_STOP_FN;

typedef struct _NS_HELPER_ATTRIBUTES
{
    union {
        struct
        {
            DWORD dwVersion;
            DWORD dwReserved;
        };
        ULONGLONG _ullAlign;
    };
    GUID guidHelper;              // GUID associated with the helper
    PNS_HELPER_START_FN pfnStart; // Function to start this helper
    PNS_HELPER_STOP_FN pfnStop;   // Function to stop this helper
} NS_HELPER_ATTRIBUTES, *PNS_HELPER_ATTRIBUTES;

typedef DWORD(WINAPI NS_CONTEXT_DUMP_FN)(IN LPCWSTR pwszRouter,
                                         _In_reads_(dwArgCount) LPWSTR *ppwcArguments,
                                         _In_ DWORD dwArgCount,
                                         IN LPCVOID pvData);

typedef NS_CONTEXT_DUMP_FN *PNS_CONTEXT_DUMP_FN;

#pragma warning(disable : 4201)
typedef struct _NS_CONTEXT_ATTRIBUTES
{
    union {
        struct
        {
            DWORD dwVersion;
            DWORD dwReserved;
        };
        ULONGLONG _ullAlign;
    };

    LPWSTR pwszContext;                      // Name of the context
    GUID guidHelper;                         // GUID of the helper servicing this context
    DWORD dwFlags;                           // Flags limiting when context is available. (See CMD_FLAG_xxx)
    ULONG ulPriority;                        // Priority field is only relevant if CMD_FLAG_PRIORITY is set in dwFlags
    ULONG ulNumTopCmds;                      // Number of top-level commands
    struct _CMD_ENTRY (*pTopCmds)[];         // Array of top-level commands
    ULONG ulNumGroups;                       // Number of command groups
    struct _CMD_GROUP_ENTRY (*pCmdGroups)[]; // Array of command groups

    PNS_CONTEXT_DUMP_FN pfnDumpFn;
    PVOID pReserved;

} NS_CONTEXT_ATTRIBUTES, *PNS_CONTEXT_ATTRIBUTES;

typedef DWORD(WINAPI FN_HANDLE_CMD)(IN LPCWSTR pwszMachine,
                                    _Inout_updates_(dwArgCount) LPWSTR *ppwcArguments,
                                    IN DWORD dwCurrentIndex,
                                    IN DWORD dwArgCount,
                                    IN DWORD dwFlags,
                                    IN LPCVOID pvData,
                                    OUT BOOL *pbDone);

typedef FN_HANDLE_CMD *PFN_HANDLE_CMD;

typedef struct _CMD_ENTRY
{
    LPCWSTR pwszCmdToken;         // The token for the command
    PFN_HANDLE_CMD pfnCmdHandler; // The function which handles this command
    DWORD dwShortCmdHelpToken;    // The short help message
    DWORD dwCmdHlpToken; // The message to display if the only thing after the command is a help token (HELP, /?, -?, ?)
    DWORD dwFlags;       // Flags (see CMD_FLAGS_xxx above)
    PVOID pOsVersionCheck; // Check for the version of the OS this command can run against
} CMD_ENTRY, *PCMD_ENTRY;

typedef struct _CMD_GROUP_ENTRY
{
    LPCWSTR pwszCmdGroupToken; // The token for the command verb
    DWORD dwShortCmdHelpToken; // The message to display in a command listing.
    ULONG ulCmdGroupSize;      // The number of entries in the cmd table
    DWORD dwFlags;             // Flags (see CMD_FLAG_xxx)
    PCMD_ENTRY pCmdGroup;      // The command table
    PVOID pOsVersionCheck;     // Check for the version of the OS this command can run against
} CMD_GROUP_ENTRY, *PCMD_GROUP_ENTRY;

typedef enum _NS_REQS
{
    NS_REQ_ZERO = 0,
    NS_REQ_PRESENT = 1,
    NS_REQ_ALLOW_MULTIPLE = 2,
    NS_REQ_ONE_OR_MORE = 3
} NS_REQS;

typedef struct _TAG_TYPE
{
    LPCWSTR pwszTag;  // tag string
    DWORD dwRequired; // required or not
    BOOL bPresent;    // present or not
} TAG_TYPE, *PTAG_TYPE;

void PrintError(HMODULE, UINT ResourceId);
void PrintMessageFromModule(HMODULE, UINT ResourceId);
DWORD PreprocessCommand(HANDLE,
                        LPWSTR *ppwcArguments,
                        DWORD dwCurrentIndex,
                        DWORD dwArgCount,
                        TAG_TYPE *pttTags,
                        DWORD dwTagCount,
                        DWORD dwMinArgs,
                        DWORD dwMaxArgs,
                        DWORD *pdwTagType);
DWORD RegisterContext(IN CONST NS_CONTEXT_ATTRIBUTES *pChildContext);
// both parameters are dummy
DWORD WINAPI NetKVMNetshStartHelper(const GUID *, DWORD);
// the parameter is dummy
DWORD WINAPI NetKVMNetshStopHelper(DWORD);
