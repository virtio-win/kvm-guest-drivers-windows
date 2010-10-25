#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "precomp.h"
#include <string>
#include <vector>

using std::wstring;
using std::vector;

class CController {
public:
    CController(LPCTSTR linkname, LPCTSTR friendlyname);
    ~CController();
    BOOL EnumPorts();
protected:
    wstring m_linkname;
    wstring m_friendlyname;
};

class CControllerList {
public:
    CControllerList(const GUID& guid);
    ~CControllerList();
    int FindControlles();
protected:
    GUID m_guid;
    vector<CController> m_controllers;
};

#endif

