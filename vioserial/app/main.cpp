//#include <basetyps.h>
#include "device.h"
#include "assert.h"

#pragma warning(default:4201)

BOOL
GetInfoTest(
    __in CDevice *pDev
    )
{
    PVOID   buf = NULL;
    PVIRTIO_PORT_INFO inf = NULL;
    size_t  len; 
    
    if (!pDev) return FALSE;

    len = sizeof(VIRTIO_PORT_INFO);
    buf = GlobalAlloc(0, len);
    if (!pDev->GetInfo(buf, &len))
    {
        GlobalFree(buf);
        buf = GlobalAlloc(0, len);
        if (!pDev->GetInfo(buf, &len))
        {
           free (buf);
           return FALSE;
        }
    }

    inf = (PVIRTIO_PORT_INFO)buf;
    printf("Id = %d\n", inf->Id);
    printf("OutVqFull = %d\n", inf->OutVqFull);
    printf("HostConnected = %d\n", inf->HostConnected);
    printf("GuestConnected = %d\n", inf->GuestConnected);
    if (len > sizeof(VIRTIO_PORT_INFO) && inf->Name)
    {
        printf("Id = %s\n", inf->Name);
    }
    GlobalFree(buf);
    return TRUE;
}

BOOL
WriteTest(
    __in CDevice *pDev,
    __in BOOLEAN ovrl
    )
{
    PUCHAR  buf = NULL;
    BOOLEAN res = TRUE;
    int     i;
    size_t  size = 4096;

    if (!pDev) return FALSE;

    printf("%s.\n", __FUNCTION__);


    buf = (PUCHAR)malloc(size);

    if( buf == NULL )
    {
        printf("%s: Could not allocate %d "
               "bytes buf\n", __FUNCTION__, size);

        return FALSE;
    }

    for(i = 0 ;i < size; i++)
    {
        int ch = getchar();

        buf[i] = ch;
        if (ch == '\n') break;
    }

    res =  ovrl ? pDev->WriteEx(buf, &size) : pDev->Write(buf, &size);

    if (!res)
    {
        printf ("%s: WriteFile failed: "
                "Error %d\n", __FUNCTION__, GetLastError());
    }
    else
    {
        printf ("%s: WriteFile OK: "
                "snd %d bytes\n\n", __FUNCTION__, size);
        printf ("%s\n", buf);
    }

    free (buf);

    return res;
}

BOOL
ReadTest(
    __in CDevice *pDev,
    __in BOOLEAN ovrl
    )
{
    PUCHAR buf = NULL;
    BOOLEAN res = TRUE;

    size_t size = 4096;

    if (!pDev) return FALSE;

    printf("%s.\n", __FUNCTION__);


    buf = (PUCHAR)malloc(size);

    if( buf == NULL )
    {
        printf("%s: Could not allocate %d "
               "bytes buf\n", __FUNCTION__, size);

        return FALSE;
    }

    res =  ovrl ? pDev->ReadEx(buf, &size) : pDev->Read(buf, &size);

    if (!res)
    {
        printf ("%s: ReadFile failed: "
                "Error %d\n", __FUNCTION__, GetLastError());
    }
    else
    {
        printf ("%s: ReadFile OK: "
                "rcv %d bytes\n\n", __FUNCTION__, size);
        printf ("%s\n", buf);
    }

    free (buf);

    return res;
}

ULONG
_cdecl
wmain(
    __in              ULONG argc,
    __in_ecount(Argc) PWCHAR argv[]
    )
{
    int ch;
    CDevice *m_pDev;
    BOOL stoptest = FALSE;
    BOOL ovrl = FALSE;

    if(argc == 2)
    {
        if (_wcsicmp(L"-o", argv[1]) == 0) {
           ovrl = TRUE;
        }
    }

    if (ovrl)
    {
        printf("Running in non-blocking mode.\n");
    }
    else
    {
        printf("Running in blocking mode.\n");
    }

    m_pDev = new CDevice;
    if (!m_pDev) 
    {
        return 1;
    }
    if (!m_pDev->Init(ovrl))
    {
        delete m_pDev;
        return 2;
    }

    while (!stoptest)
    {
        ch = getchar();
        while(getchar()!='\n');
        switch (ch)
        {
           case 'i':
           case 'I':
              GetInfoTest(m_pDev);
              break;
           case 'r':
           case 'R':
              ReadTest(m_pDev, ovrl);
              break;
           case 'w':
           case 'W':
              WriteTest(m_pDev, ovrl);
              break;
           case 'q':
           case 'Q':
              stoptest = TRUE;
              break;
        }
    }

    delete m_pDev;
    return 0;
}
