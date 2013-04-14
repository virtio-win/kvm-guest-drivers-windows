#include "stdafx.h"
#include "testcommands.h"

#undef malloc
#undef free

#define MAX_PARAMS 4

static bool bFailed;

class tDeallocator : public CMap<void *, void *, void *, void *>
{
public:
    void FreeAllEntries()
    {
        POSITION pos = GetStartPosition();
        void *p, *pp;
        while (pos)
        {
            GetNextAssoc(pos, p, pp);
            if (p == pp) free(p);
            else
                FailCase("ERROR: freeing invalid pointer %p\n", p);
        }
    }
};

static tDeallocator deallocator;

EXTERN_C void *AllocateMemory(size_t size)
{
    void *p = malloc(size);
    if (p) deallocator.SetAt(p, p);
    return p;
}

EXTERN_C void DeallocateMemory(void *p)
{
    void *pp;
    if (deallocator.Lookup(p, pp) && pp == p)
    {
        deallocator.RemoveKey(p);
        free(p);
    }
    else
    {
        FailCase("ERROR: freeing invalid pointer %p\n", p);
    }
}


void    FailCase(const char* format, ...)
{
    va_list list;
    CString s;
    va_start(list, format);
    LogTestFlow("FAILED:\n");
    s.FormatV(format, list);
    s += "\n";
    LogTestFlow("%s", (LPCSTR)s);
    bFailed = TRUE;
}

static bool IsSuitableCharForString(char c)
{
    return isalnum(c) || c == '_' || c == '.' || c == '$';
}

typedef enum _tParamType
{
    ptNone,
    ptInteger,
    ptIntegerRef,
    ptString
}tParamType;

typedef bool (*tParameterQueryCallback)(PVOID context, LPCSTR name, ULONG& val);

class tParameter
{
public:
    tParamType _type;
    tParameter() { _type = ptNone; }
    tParameter(UINT v)
    {
        _type = ptInteger;
        integerValue = v;
        content.Format("%d", v);
    }
    tParameter(const tParameter& param)
    {
        *this = param;
    }
    void operator = (const tParameter& param)
    {
        _type = param._type;
        integerValue = param.integerValue;
        content = param.content;
        queryContext = param.queryContext;
        queryCallback = param.queryCallback;
    }
    bool IsString() const {return _type == ptString; }
    LPCSTR String() const {return content; }
    CString Printable() const
    {
        CString s;
        switch(_type)
        {
            case ptNone: s = ""; break;
            case ptInteger: s.Format("%d", integerValue); break;
            case ptString: s.Format("\"%s\"", content); break;
            case ptIntegerRef: s.Format("\"$%s\"", content); break;
            default: s = "OOPS"; break;
        }
        return s;
    }
    const tParameter& operator = (const char *s)
    {
        _type = ptString;
        content = s;
        return *this;
    }
    const tParameter& operator = (ULONG val)
    {
        _type = ptInteger;
        content.Format("%d", val);
        integerValue = val;
        return *this;
    }
    void Refer(LPCSTR s, tParameterQueryCallback _callback, PVOID context)
    {
        _type = ptIntegerRef;
        content = s;
        queryCallback = _callback;
        queryContext = context;
    }
    ULONG Value()
    {
        if (_type == ptInteger) return integerValue;
        if (_type == ptIntegerRef)
        {
            if (queryCallback(queryContext, content, integerValue))
                return integerValue;
            else
                FailCase("Can't query value for %s", String());
            return 0;
        }
        FailCase("Can't return value for parameter type %d", _type);
        return 0;
    }
protected:
    CString content;
    union
    {
        ULONG integerValue;
    };
    tParameterQueryCallback queryCallback;
    PVOID queryContext;
};

typedef enum _tCommandId
{
    cmdInvalid,
    cmdComment,
    cmdLabel,
    cmdGoto,
    cmdEnd,
    cmdIf,
    cmdSet,
    cmdEval,
    cmdAdd,
    cmdPreprocess,
    cmdPrepare,
    cmdTxAsync,
    cmdSend,
    cmdTxGet,
    cmdTxComplete,
    cmdRxReturn,
    cmdRxReceive,
    cmdRxRestart,
    cmdTxRestart,
    cmdTxEnableInterrupt,
    cmdTxDisableInterrupt,
    cmdRxEnableInterrupt,
    cmdRxDisableInterrupt,
    cmdRxGet,
    cmdDataGet,
    cmdDataSet,
    cmdControlMacTable,
    cmdControlRxMode,
    cmdControlVlanAdd,
    cmdControlVlanDel,
}tCommandId;

class CParametersArray : public CArray<tParameter *>
{
public:
    CParametersArray() {}
    CParametersArray(const CParametersArray& a)
    {
        int i;
        for (i = 0; i < a.GetCount(); ++i)
        {
            tParameter *pParam = new tParameter(*a[i]);
            Add(pParam);
        }
    }
};


class tCommand
{
public:
    tCommandId cmdId;
    ULONG step;
    ULONG line;
    ULONG lineDot;
    ULONG elseStepForIf;
    CParametersArray params;
    tCommand(tCommandId id = cmdInvalid) {cmdId = id; step = 0; }
    const tCommand& operator = (const tCommand& v)
    {
        int i;
        FreeParams();
        cmdId = v.cmdId;
        for (i = 0; i < v.params.GetCount(); ++i)
        {
            tParameter *pParam = new tParameter(*v.params[i]);
            params.Add(pParam);
        }
        step = v.step;
        line = v.line;
        lineDot = v.lineDot;
        return *this;
    }
    CString Description();

    ~tCommand()
    {
        FreeParams();
    }
protected:
    void FreeParams()
    {
        int i;
        for (i = 0; i < params.GetCount(); ++i)
        {
            delete params[i];
        }
    }
};

typedef struct _tLabel
{
    CString name;
    ULONG index;
    _tLabel(const char *_name, const tCommand& _cmd)
    {
        name = _name;
        index = _cmd.step;
    }
}tLabel;

typedef struct _tCommandDecs
{
    const char *description;
    tCommandId cmd;
    const char *token;
    tParamType types[MAX_PARAMS];
}tCommandDescription;


static void EmptyCallback(PVOID ref, tScriptEvent evt, const char *format, ...)
{
}

class tScriptState
{
protected:
    ULONG result;
    FILE *f;
    int currentCommandIndex;
    ULONG currentLine;
    ULONG currentDot;
    CList<tLabel *> Labels;
    CArray<tCommand *> Commands;
    PVOID ref;
    tScriptCallback callback;
    CString name;
    CMap<CString, LPCSTR, ULONG, ULONG> Variables;
    void PreprocessScript(bool& bSilent);
    void SetVariable(LPCSTR s, ULONG value);
    bool EvaluateCondition(CString s);
    bool PreprocessCommand(tCommand& cmd, CString& s, bool& bSilent);
    bool ExecuteCommand(tCommand& cmd);
    bool FindLabelByName(const tParameter& param, ULONG *pStep = NULL);
    bool GetParam(CString& s, tParamType paramType, CString& dest, tParameter& param);
    bool FillParameters(CString& s, tCommand& cmd, const _tCommandDecs& cmdDesc);
    void ParseCommand(CString& s, tCommand& cmd, ULONG step, bool& bSilent);
    ULONG ReadCommand(CString& s);
    tCommand GetNextCommand(CString& sLine, bool& bSilent);
public:
    tScriptState(const char *_name, PVOID _ref, tScriptCallback _callback)
    {
        result = 0;
        f = NULL;
        currentCommandIndex = 0;
        ref = _ref;
        callback = _callback;
        name = _name;
        currentLine = 0;
        currentDot  = 0;
        if (!callback) callback = EmptyCallback;
    }
    bool Run();
    bool GetVariable (LPCSTR s, ULONG& value)  const;
    bool IsFailed() const { return bFailed;}
    ~tScriptState()
    {
        int i;
        for (i = 0; i < Commands.GetCount(); ++i)
        {
            tCommand *pCmd = Commands.GetAt(i);
            delete pCmd;
        }
        POSITION pos = Labels.GetHeadPosition();
        while (pos)
        {
            tLabel *pLabel = Labels.GetAt(pos);
            delete pLabel;
            Labels.GetNext(pos);
        }
    }
};

static tCommandDescription commands[] =
{
    /* ------- standard part of commands ---------*/
    { "comment", cmdComment, ";" },
    { "comment", cmdComment, "//" },
    { "label", cmdLabel, ":", {ptString }},
    { NULL, cmdEnd, "exit" },
    { NULL, cmdGoto, "goto", {ptString }},
    { NULL, cmdIf, "if", {ptString, ptString } },
    { NULL, cmdEnd, "end" },
    { NULL, cmdSet, "set", {ptString, ptInteger } },
    { NULL, cmdAdd, "add", {ptString, ptInteger } },
    { NULL, cmdPreprocess, ".preprocess", {ptString} },
    /* ------- custom part of commands ---------*/
    { NULL, cmdPrepare, "prepare" },
    { NULL, cmdTxAsync, "txasync", {ptInteger} },
    { NULL, cmdSend, "send", {ptInteger, ptInteger} },
    { NULL, cmdTxComplete, "txcomplete", {ptInteger}},
    { NULL, cmdTxGet, "txget", {ptInteger}},
    { NULL, cmdTxRestart, "txrestart", {ptInteger} },
    { NULL, cmdTxEnableInterrupt, "txen" },
    { NULL, cmdTxDisableInterrupt, "txdis" },
    { NULL, cmdRxReceive, "recv", {ptInteger} },
    { NULL, cmdRxGet, "rxget", {ptInteger}},
    { NULL, cmdRxReturn, "rxret", {ptInteger} },
    { NULL, cmdRxRestart, "rxrestart", {ptInteger} },
    { NULL, cmdRxEnableInterrupt, "rxen" },
    { NULL, cmdRxDisableInterrupt, "rxdis" },
    { NULL, cmdDataGet, "dataget", {ptInteger, ptInteger} },
    { NULL, cmdDataSet, "dataset", {ptInteger, ptInteger} },
    { NULL, cmdControlRxMode, "control.rxmode", {ptInteger, ptInteger} },
    { NULL, cmdControlMacTable, "control.mac", {ptInteger} },
    { NULL, cmdControlVlanAdd, "control.addvlan", {ptInteger} },
    { NULL, cmdControlVlanDel, "control.delvlan", {ptInteger} },
};

static const char *FindToken(tCommandId id)
{
    int i;
    for (i = 0; i < ELEMENTS_IN(commands); ++i)
        if (commands[i].cmd == id) return (commands[i].token);
    return NULL;
}

static const char *FindDescription(tCommandId id)
{
    int i;
    for (i = 0; i < ELEMENTS_IN(commands); ++i)
        if (commands[i].cmd == id) return (commands[i].description) ? commands[i].description : commands[i].token;
    return NULL;
}

static ULONG NumberOfParameters(const _tCommandDecs& cmdDesc)
{
    ULONG n = 0, i, nMax = 0;
    for (i = 0; i < ELEMENTS_IN(cmdDesc.types); ++i)
    {
        if (cmdDesc.types[i] != ptNone) nMax = i + 1;
    }
    return nMax;
}

static ULONG FindNumberOfParameters(tCommandId id)
{
    int i;
    for (i = 0; i < ELEMENTS_IN(commands); ++i)
        if (commands[i].cmd == id) return NumberOfParameters(commands[i]);
    return MAX_PARAMS;
}

ULONG tScriptState::ReadCommand(CString& s)
{
    char buf[256];
    if (!fgets(buf, sizeof(buf) - 1, f))
    {
        if (feof(f)) s = FindToken(cmdEnd);
        else s = "<Error reading from file>";
    }
    else
    {
        s = buf;
    }
    while (isspace(s[0])) s.Delete(0, 1);
    s.Remove('\n');
    s.Remove('\r');
    currentLine++;
    currentDot = 0;
    strcpy(buf, s);
    return currentCommandIndex++;
}

static bool QueryVariable(PVOID context, LPCSTR name, ULONG& val)
{
    tScriptState *ps = (tScriptState *)context;
    return ps->GetVariable(name, val);
}

bool tScriptState::GetParam(CString& s, tParamType paramType, CString& dest, tParameter& param)
{
    bool bOK = FALSE;
    dest.Empty();
    while (isspace(s[0])) s.Delete(0, 1);
    switch (paramType)
    {
        case ptString:
            if ((s[0] == '\"') || s[0] == '\'')
            {
                char endchar = s[0];
                s.Delete(0, 1);
                int pos = s.Find(endchar);
                if (pos > 0)
                {
                    while (IsSuitableCharForString(s[0]) || isspace(s[0])) { dest += s[0]; s.Delete(0, 1); }
                    bOK = s[0] == endchar;
                    if (bOK)
                    {
                        s.Delete(0, 1);
                        while (isspace(dest[dest.GetLength() - 1])) dest.Delete(dest.GetLength() - 1, 1);
                    }
                }
            }
            else
            {
                while (IsSuitableCharForString(s[0])) { dest += s[0]; s.Delete(0, 1); }
            }
            bOK = !dest.IsEmpty();
            param = dest;
            break;
        case ptInteger:
            if (s[0] == '-') { dest += s[0]; s.Delete(0, 1); }
            while (isdigit(s[0])) { dest += s[0]; s.Delete(0, 1); }
            bOK = !dest.IsEmpty();
            param = dest;
            if (bOK)
            {
                char *end = NULL;
                _set_errno(0);
                int val = strtoul(dest.GetBuffer(), &end, 10);
                bOK = !errno;
                if (bOK) param = (ULONG)val;
            }
            else if (s[0] == '$')
            {
                s.Delete(0,1);
                while (IsSuitableCharForString(s[0])) { dest += s[0]; s.Delete(0, 1); }
                param.Refer(dest, QueryVariable, this);
                bOK = TRUE;
            }
            break;
        case ptNone:
            bOK = TRUE;
            break;
        default:
            break;
    }
    return bOK;
}


bool tScriptState::FillParameters(CString& s, tCommand& cmd, const _tCommandDecs& cmdDesc)
{
    CString sParam;
    bool bOK = TRUE;
    ULONG i, n = NumberOfParameters(cmdDesc);
    for (i = 0; bOK && i < n; ++i)
    {
        tParameter *pParam = new tParameter;
        bOK = GetParam(s, cmdDesc.types[i], sParam, *pParam);
        if (bOK) cmd.params.Add(pParam);
        else delete pParam;
    }
    if (!bOK)
    {
        FailCase("Command %04d:%s Can't retrieve parameter %d from \"%s\"",
        cmd.step,
        (LPCSTR)cmd.Description(),
        i,
        s.GetBuffer());
    }
    return bOK;
}


CString tCommand::Description()
{
    CString s;
    const char *description = FindDescription(cmdId);
    ULONG i, n = FindNumberOfParameters(cmdId);
    if ((int)n > params.GetCount()) n = params.GetCount();
    s = description;
    s += '(';
    for (i = 0; i < n; ++i)
    {
        s += (LPCSTR)params[i]->Printable();
        s += ',';
    }
    if (s[s.GetLength() - 1] == ',') s.Delete(s.GetLength() - 1, 1);
    s += ')';
    return s;
}

void tScriptState::ParseCommand(CString& s, tCommand& cmd, ULONG step, bool& bSilent)
{
    size_t len = 0;
    int i, selected = -1;
    const char *token;
    tCommandId cmdId = cmdInvalid;
    cmd.step =  step;
    cmd.line = currentLine;
    cmd.lineDot = ++currentDot;
    s.MakeLower();
    for (i = 0; i < ELEMENTS_IN(commands); ++i)
    {
        if (s.Find(commands[i].token) == 0 && len < strlen(commands[i].token))
        {
            cmdId = commands[i].cmd;
            len = strlen(commands[i].token);
            token = commands[i].token;
            selected = i;
        }
    }
    cmd.cmdId = cmdId;
    if (cmdId != cmdInvalid)
    {
        s.Delete(0, len);
        // get parameters
        if (FillParameters(s, cmd, commands[selected]))
        {
            if (!bSilent)
            {
                s.Format("%04d.%d(%04d) Command %s", cmd.line, cmd.lineDot, cmd.step, (LPCSTR)cmd.Description());
                LogTestFlow("%s\n", (LPCSTR)s);
            }
        }
    }
}

tCommand tScriptState::GetNextCommand(CString& sLine, bool& bSilent)
{
    tCommand cmd;
    ULONG step = ReadCommand(sLine);
    if (!sLine.IsEmpty())
    {
        CString s;
        s = sLine;
        ParseCommand(s, cmd, step, bSilent);
    }
    else
    {
        cmd.cmdId = cmdComment;
        cmd.line  = currentLine;
        cmd.lineDot = currentDot;
    }
    return cmd;
}


bool tScriptState::FindLabelByName(const tParameter& param, ULONG *pStep)
{
    if (param._type != ptString) return FALSE;
    if (!param.IsString()) return FALSE;
    if (!Labels.GetCount()) return FALSE;
    POSITION pos = Labels.GetHeadPosition();
    while (pos)
    {
        tLabel *pLabel = Labels.GetAt(pos);
        if (!pLabel->name.CompareNoCase(param.String()))
        {
            if (pStep) *pStep = pLabel->index;
            return TRUE;
        }
        Labels.GetNext(pos);
    }

    return FALSE;
}

bool tScriptState::EvaluateCondition(CString s)
{
    bool b = FALSE;
    CString sPrintable;
    CString sTemp;
    sPrintable.Format("Evaluating %s", (LPCSTR)s );
    if (!s.CompareNoCase("true"))
    {
        b = TRUE;
    }
    else if (!s.CompareNoCase("false"))
    {

    }
    else
    {
        bool bOK;
        tCommand cmd;
        cmd.cmdId = cmdEval;
        _tCommandDecs desc = { NULL, cmdEval, "eval", {ptString, ptString, ptInteger } };
        if (FillParameters(s, cmd, desc))
        {
            ULONG val, operand;
            CString op = cmd.params[1]->String();
            operand = cmd.params[2]->Value();
            bOK = GetVariable(cmd.params[0]->String(), val);
            if (bOK)
            {
                sTemp.Format("(%s = %d)", (LPCSTR)cmd.params[0]->String(), val);
                sPrintable += sTemp;
                if (!op.CompareNoCase("lt"))
                {
                    b = val < operand;
                }
                else if (!op.CompareNoCase("gt"))
                {
                    b = val > operand;
                }
                else if (!op.CompareNoCase("le"))
                {
                    b = val <= operand;
                }
                else if (!op.CompareNoCase("ge"))
                {
                    b = val >= operand;
                }
                else if (!op.CompareNoCase("eq"))
                {
                    b = val == operand;
                }
                else if (!op.CompareNoCase("ne"))
                {
                    b = val != operand;
                }
            }
            else
            {
                FailCase("Variable %s does not exist", cmd.params[0]->String());
            }
        }
    }
    sTemp.Format("=%d", b);
    sPrintable += sTemp;
    LogTestFlow("%s\n", (LPCSTR)sPrintable);
    return b;
}

bool tScriptState::GetVariable(LPCSTR s, ULONG& value) const
{
    bool b = !!Variables.Lookup(s, value);
    return b;
}

void tScriptState::SetVariable(LPCSTR s, ULONG value)
{
    Variables.SetAt(s, value);

    if (!strcmp(s, "use_indirect"))
        bUseIndirectTx = !!value;
}

// returns FALSE on END command, TRUE otherwise
bool tScriptState::ExecuteCommand(tCommand& cmd)
{
    bool bContinue = TRUE;
    switch(cmd.cmdId)
    {
        case cmdLabel:
            break;
        case cmdGoto:
            {
                ULONG step;
                const tParameter *pParam = cmd.params.GetAt(0);
                if (!FindLabelByName(*pParam, &step))
                {
                    FailCase("Can't find label %s", (LPCSTR)pParam->String());
                }
                else
                {
                    currentCommandIndex = step;
                }
            }
            break;
        case cmdIf:
            {
                bool bTrue = EvaluateCondition(cmd.params[0]->String());
                if (!bTrue)
                {
                    currentCommandIndex = cmd.elseStepForIf;
                }
            }
            break;
        case cmdEnd:
            bContinue = FALSE;
            break;
        case cmdSet:
            {
                ULONG data;
                ULONG value = cmd.params[1]->Value();
                if (!GetVariable(cmd.params[0]->String(), data) || data != value)
                {
                    data = value;
                    SetVariable(cmd.params[0]->String(), data);
                }
            }
            break;
        case cmdAdd:
            {
                ULONG data;
                if (GetVariable(cmd.params[0]->String(), data))
                {
                    data += cmd.params[1]->Value();
                    SetVariable(cmd.params[0]->String(), data);
                }
                else
                {
                    FailCase("Can't add %s", cmd.params[0]->String());
                }
            }
            break;
        case cmdPrepare:
            {
                ULONG data;
                if (GetVariable("use_merged_buffers", data))
                    bUseMergedBuffers = !!data;
                if (GetVariable("use_published_events", data))
                    bUsePublishedIndices = !!data;
                if (GetVariable("use_msix", data))
                    bMSIXUsed = !!data;
                SimulationPrepare();
            }
            break;
        case cmdTxAsync:
            {
                bAsyncTransmit = !!cmd.params[0]->Value();
            }
            break;
        case cmdSend:
            {
                AddTxBuffers(cmd.params[0]->Value(), cmd.params[1]->Value());
            }
            break;
        case cmdTxComplete:
            {
                CompleteTx((int)cmd.params[0]->Value());
            }
            break;
        case cmdTxGet:
            {
                GetTxBuffer(cmd.params[0]->Value());
            }
            break;
        case cmdRxReturn:
            {
                ReturnRxBuffer(cmd.params[0]->Value());
            }
            break;
        case cmdRxReceive:
            {
                RxReceivePacket((UCHAR)cmd.params[0]->Value());
            }
            break;
        case cmdDataGet:
            {
                UCHAR offset, val, res;
                offset = (UCHAR)cmd.params[0]->Value();
                val = (UCHAR)cmd.params[1]->Value();
                res = GetDeviceData(offset);
                if (res != val)
                {
                    FailCase("cmdDataGet(%d) = %d, expected %d", offset, res, val);
                }
            }
            break;
        case cmdDataSet:
            {
                UCHAR offset, val;
                offset = (UCHAR)cmd.params[0]->Value();
                val = (UCHAR)cmd.params[1]->Value();
                SetDeviceData(offset, val);
            }
            break;

        case cmdRxRestart:
            {
                BOOLEAN bExpected = (BOOLEAN)cmd.params[0]->Value();
                BOOLEAN b = RxRestart();
                if (b != bExpected)
                {
                    FailCase("RxRestart: %d, expected %d", b, bExpected);
                }
            }
            break;
        case cmdTxRestart:
            {
                BOOLEAN bExpected = (BOOLEAN)cmd.params[0]->Value();
                BOOLEAN b = TxRestart();
                if (b != bExpected)
                {
                    FailCase("TxRestart: %d, expected %d", b, bExpected);
                }
            }
            break;
        case cmdTxEnableInterrupt:
            {
                TxEnableInterrupt();
            }
            break;
        case cmdTxDisableInterrupt:
            {
                TxDisableInterrupt();
            }
            break;
        case cmdRxEnableInterrupt:
            {
                RxEnableInterrupt();
            }
            break;
        case cmdRxDisableInterrupt:
            {
                RxDisableInterrupt();
            }
            break;
        case cmdRxGet:
            {
                ULONG len;
                GetRxBuffer(&len);
            }
            break;
        case cmdControlRxMode:
            {
                SetRxMode((UCHAR)cmd.params[0]->Value(), cmd.params[1]->Value() != 0);
            }
            break;
        case cmdControlVlanAdd:
            {
                USHORT us = (USHORT)cmd.params[0]->Value();
                VlansAdd(&us, 1);
            }
            break;
        case cmdControlVlanDel:
            {
                USHORT us = (USHORT)cmd.params[0]->Value();
                VlansDel(&us, 1);
            }
            break;
        case cmdControlMacTable:
            {
                SetMacAddresses(cmd.params[0]->Value());
            }
        default:
            result = 1;
            break;
    }
    return bContinue;
}

bool tScriptState::PreprocessCommand(tCommand& cmd, CString& s, bool& bSilent)
{
    bool bAdd = FALSE;
    bool bEnd = FALSE;
    switch (cmd.cmdId)
    {
        case cmdInvalid:
            FailCase("Invalid line: %s", s.GetBuffer());
            break;
        case cmdPreprocess:
            if (!strcmp(cmd.params[0]->String(), "loud"))
                bSilent = FALSE;
            if (!strcmp(cmd.params[0]->String(), "noisy"))
                bSilent = FALSE;
            if (!strcmp(cmd.params[0]->String(), "quiet"))
                bSilent = TRUE;
            break;
        case cmdEnd:
            bEnd = TRUE;
            bAdd = TRUE;
            break;
        case cmdLabel:
            if (FindLabelByName(*cmd.params[0]))
            {
                FailCase("Can't add label %s", cmd.params[0]->String());
            }
            else
            {
                tLabel *newLabel = new tLabel(cmd.params[0]->String(), cmd);
                Labels.AddTail(newLabel);
            }
            bAdd = TRUE;
            break;
        case cmdIf:
            {
                tCommand nestedCommand;
                bool bIsBad;
                CString sNested = cmd.params[1]->String();
                CString sCopy = sNested;
                ParseCommand(sNested, nestedCommand, currentCommandIndex++, bSilent);
                bIsBad = nestedCommand.cmdId == cmdLabel || nestedCommand.cmdId == cmdComment;
                if (bIsBad)
                {
                    FailCase("Invalid nested command %s", (LPCSTR)sCopy);
                }
                else
                {
                    cmd.elseStepForIf = currentCommandIndex;
                    tCommand *pNewCommand = new tCommand(cmd);
                    Commands.Add(pNewCommand);
                    PreprocessCommand(nestedCommand, sCopy, bSilent);
                }
            }
            break;
        default:
            bAdd = TRUE;
            break;
    }

    if (bAdd)
    {
        tCommand *pNewCommand = new tCommand(cmd);
        Commands.Add(pNewCommand);
    }
    return bEnd;
}

void tScriptState::PreprocessScript(bool& bSilent)
{
    bool bEnd = FALSE;
    LogTestFlow("Preprocessing...\n");
    while (!IsFailed() && !bEnd)
    {
        CString s;
        tCommand cmd = GetNextCommand(s, bSilent);
        bEnd = PreprocessCommand(cmd, s, bSilent);
    }
    LogTestFlow("Preprocessing %s\n", IsFailed() ? "failed" : "done" );
}


BOOLEAN RunScript(const char *name, PVOID ref, tScriptCallback callback)
{
    BOOLEAN b;
    tScriptState state(name, ref, callback);
    b = state.Run();
    SimulationFinish();
    deallocator.FreeAllEntries();
    return b;
}

bool tScriptState::Run()
{
    bool bResult = FALSE;
    f = fopen(name, "rt");
    if (f)
    {
        bool bSilent = TRUE;
        PreprocessScript(bSilent);
        fclose(f);
        if (!IsFailed())
        {
            currentCommandIndex = 0;
            bResult = TRUE;
            while (bResult)
            {
                tCommand *pCommand = NULL;
                if (currentCommandIndex < Commands.GetCount())
                    pCommand = Commands.GetAt(currentCommandIndex);
                if (pCommand)
                {
                    LogTestFlow("Executing %04d.%d(%04d) %s\n", pCommand->line, pCommand->lineDot, currentCommandIndex, (LPCSTR)pCommand->Description() );
                    currentCommandIndex++;
                    if (!ExecuteCommand(*pCommand))
                        break;
                    bResult = !IsFailed();
                }
                else
                {
                    break;
                }
            }
        }
    }
    else
    {
        callback(ref, escriptEvtPreprocessFail, "Can't open %s", name);
        bResult = FALSE;
    }
    return bResult;
}



static CMap<ULONG, ULONG, void *, void *> QueueOfRxPackets;

void KeepRxPacket(void *buffer, ULONG serial)
{
    void *p;
    if (QueueOfRxPackets.Lookup(serial, p))
    {
        FailCase("[%s] - packet %d already exists!", __FUNCTION__, serial);
        QueueOfRxPackets.RemoveKey(serial);
    }
    QueueOfRxPackets.SetAt(serial, buffer);
}

void *GetRxPacket(ULONG serial)
{
    void *p = NULL;
    if (QueueOfRxPackets.Lookup(serial, p))
    {
        QueueOfRxPackets.RemoveKey(serial);
        return p;
    }
    else
    {
        FailCase("[%s] - packet %d does not exist!", __FUNCTION__, serial);
        return NULL;
    }
}

