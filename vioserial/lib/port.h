#ifndef PORT_H
#define PORT_H

#include "precomp.h"
#include "notifier.h"

class CPort {
public:
    CPort();
    ~CPort();
    BOOL     Read();
    BOOL     Write();
protected:
    HANDLE   m_hPort;
    CNotifier* m_pNotifier;
};

#endif

