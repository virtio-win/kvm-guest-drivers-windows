Update 2.02.2009

1. Added support for connect/disconnect indication from QEMU
2. Added minimal support for 802.1P and 802.1Q (single type 2 header, enough for WHQL testing)

==================
Update 9.01.2009

1. Vista/2008 supports scatter-gather
2. XP/2003 and Vista support LSO offload (WHQL test will not pass, but functionally works)
3. XP/2003 able to support also checksum offload: SW emulation (TCP,UDP,IP) and Hardware-assisted.
   Hardware supports only TCP/UDP checksum, so if Hardware checksumming enabled,
   the IP checksum should be turned off.
4. Default setting for both XP and Vista: SG enabled, LSO enabled, checksumming disabled.
   Note that LSO requires SG



Update 21.11

1. Extended possible RX and TX range (RX to 1024, TX to 512)
2. Added ability to indicate 10M
3. Added RX receive repeat based on VIRTIO restart status


Update 2.08

1. Enabled scatter-gather under XP
2. Passes all the tests of NDIS (pre-WHQL)

Update 18.06.2008

1. Fixed MP race of VirtIO library calls
2. Fixed OID support for NDIS 6.0 (NDIS test)
3. Fixed packet copying limited by length of packet data, regardless length of MDL chain
4. Single lock for TX and RX (for now, to be investigated on MP)

======================================================================================

Update 14.06.2008

1. Supported WPP for all the OS, except Win2K (enable it in Common/common.mak)
2. INF files uses OS decoration for automatic install from CD
3. Changed names of drivers and services to prevent conflicts with old drivers:
   the service name for XP/2K/2003 is kvmnet5
4. Added command line tool CDImage.exe (Microsoft internal utility)
5. Buildall.bat without parameters builds everything and packs into ISO image
6. Fixed Halt() of NDIS5 (BSOD under NDIS test)
7. Added configurable parameters validation (failures in NDIS registry test)
8. Added formal support for OID_PNP_ENABLE_WAKE_UP (failure in NDIS OID test)
9. Supported both 6000 and 6001 DDK

=======================================================================================
This project is a first revision of unified project, combining sources for
paravirtualized driver for:
1. Windows Vista / Server 2008
2. Windows XP / Server 2003
3. Windows 2000

Big part of sources and almost all the data structures are shared between
2008/XP/2K driver with minor differences.

Procedures, that hardly can be implemented using shared code, have different
implementation for XP/Vista and and located in files:
ParaNdis6* (for Vista/2008)
ParaNdis5* (for Xp/2003/2000)

Comments:
1. The project uses only files that under NETKVM directory, files in Parandis and Parandis6.0 considered legacy
2. KUtils is not used (irrelevant in NDIS environment)
3. Names of INF files are changed according to MS requirement for network drivers (netkvm.inf)
4. The drivers are signed during build. On the target system the certificate shall be installed once. The batch
   file for certificate installation is produced under Install directory.
5. The NetKVM project requires Windows DDK 6000. See "NetKVM\buildall.bat". 6001 will be supported later.
6. There are 3 main directories:
LH - for Ndis6 specific files
XP - for Ndis5 specific files
Common - for common files
Auxilliary:
Tools - certificates etc
IDE   - property pages for Studio 2005


7. VirtIO library

VirtIO files are currently built aside of their native directory, to avoid affecting other projects using VirtIO.
During build, VirtIO sources are copied to NetKVM/VirtIO directory, which uses different "sources" file.
Next step, which should remove illegal (for NDIS) calls (DbgPrint and PVUtils procedures for memory allocation),
will affect also VirtIO source code. See "buildall.bat" for details.

8. Changes in XP code of NDIS driver

a)The driver is completely rewritten, using mostly the same data structure as Vista driver.
b)Redundant copying and other redundant operations are removed.
c)Synchronization problems (causing freeze on Vista and, potentially, on SMP XP) removed.
d)Dangerous (actually illegal) operations of physical memory allocations of DPC are removed.
  (The physical memory allocations usually fail on DPC, and freeing of physical memory on DPC is illegal).
  If this is a point of the design (dynamic allocations of physical memory on demand) - we can think
  how to do it correctly, without side-effects from the OS.
e)The previous driver declared itself as deserialized, but behaves completely as serialized:
  When there is no room for TX packets, they are returned or silently dropped (deserialized driver must queue and send later).
  (solved).


Open issues and todo list:
1. Promiscuous mode, Packet filtering and multicast settings - TODO (host involved)
2. Power management  - TODO (host involved)
3. WPP - TODO
4. NDIS-compliant physical memory allocations - TODO
5. Final optimizations - TODO
6. "No interrupts" problem still happens. To help in dignostics, there is "DPC check" option in "Advanced" page on Vista/2008.
9. Debug level is set using NDIS-compatible calls, read from device key and controlled via "Advanced". This may cause
   misunderstanding when run 2+ adapters. Under Vista/2008 it can be solved, under XP - hardly.
10. Vista/2008 link/duplex control - TODO
11. GNU guidelines for source code - TODO
12. Massive comments in the source code - TODO
13. Precise statictics - TODO (if needed - direct/multicast/broadcast/failed etc)
14. Version in the resource file is currently set from "buildall.bat". It shall be aligned with common build
    procedure - TODO.

For any queries/suggestion/complains please contact me yvugenfi-redhat-dot-com







