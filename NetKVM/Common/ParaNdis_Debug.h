#pragma once

void ParaNdis_DebugInitialize();
void ParaNdis_DebugCleanup(PDRIVER_OBJECT  pDriverObject);
void ParaNdis_DebugRegisterMiniport(PARANDIS_ADAPTER *pContext, BOOLEAN bRegister);
