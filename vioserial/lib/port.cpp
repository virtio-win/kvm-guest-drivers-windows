#include "port.h"


CPort::CPort()
{
    m_hPort = INVALID_HANDLE_VALUE;
    m_pNotifier = NULL;
}

CPort::~CPort()
{
}
 
BOOL CPort::Read()
{
    return TRUE;
}

BOOL CPort::Write()
{
    return TRUE;
}
