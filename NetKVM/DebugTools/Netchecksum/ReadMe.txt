========================================================================
    CONSOLE APPLICATION : netchecksum Project Overview
========================================================================

This offline tester of sw-offload.c USES files from NETKVM projects:
ethernetutils.h
ndis56common.h
sw-offload.c

Preparing input files (each one expected to contain one packet) -
see the format of TXT files, source files are WireShark records.
(some cuts from the WS record required).

When they are prepared, add them to Jobs array (netchecksum.cpp).
