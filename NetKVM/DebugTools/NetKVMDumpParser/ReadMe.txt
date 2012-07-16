========================================================================
    CONSOLE APPLICATION : NetKVMDumpParser Project Overview
========================================================================

Usage currently is trivial:
NetKVMDumpParser dump-file

To be useful, must process dumps from latest version of NETKVM,
in future - from any version supporting crash callback.

Currently there is no advantage of having correct PDB file available.
It will be loaded if available at native path (where it was built) or in current directory.
(NetKVMDumpParser now does not use it, just checks whether symbols are loaded).

(of course, when analyzing the dump in WinDBG, there is AN ADVANTAGE).

In order to compile the tool please make sure that WinDBG SDK is installed.
Point include and libraries paths to appropriate locations.



