========================================================================
    CONSOLE APPLICATION : ConsoleSim
========================================================================

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
Virtio files used here are ORIGINAL files from VirtIO library
If you modify them, you affect all the projects
VirtIOPCI.c
VirtIORing.c
VirtIO_PCI.h
VirtIO.h
VirtIO_ring.h
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

Hardware code (Hardware.c) is mostly cuts from QEMU code.

Run it, for example, as "ConsoleSim.exe test-to-run.txt" or without parameters to run "text.txt"

Script organization:
Script contains:
1.Labels (for example :label1). Case insensitive.
2.Comments (start from ; or from // )
3.Commands with parameters
Parameters can be:
a) strings, with or without quotes. If spaces are inside, quotes are mandatory
b) integers - negative (with - before number) or positive (without sign)
c) referenced integers (prefixed by $)

Available commands:
1. Flow commands:
goto {string=label}
if {string=condition} {string=command to execute, usually "goto label" }
   condition has a form of "variable-name compare-operation operand"
   variable-name without $
   compare-operation: le,lt,ge,gt,eq,ne
   operand: integer or variable with $
.preprocess {command} command = loud | quiet

2. Manipulation with variables:
set {string=variable name} {integer=value} (set index 1) (set index1=$index2)
add {string=variable name} {integer=value} (add index 1) (add index1 -1) (add index $increment)

special variables:
use_published_events: to be used before "prepare" command. Default=1
use_merged_buffers: to be used before "prepare" command. Default=1
use_msix: to be used before "prepare" command. Default=0
use_indirect: Default=0. If 1, tx transmits using indirect;

3. Functional commands:
prepare - initializes the simulation. Used once.
send {integer=serial number of first packet} {integer=number of packet to send} serial will be incremented for each packet
txasync {integer=0/1} defines how the "send" will work - will the packet be completed asynchronously(1) or synchronously(0)
txcomplete {integer=n} complete n previous send operations; if n=-1 completes all
txget {integer=serial number of packet} gets completed sent packet with specified serial number.
      if packet is not completed or serial is different - produces error
txrestart {integer=expected value} calls restart operation on Tx queue
      if returned value is not equal to parameter - produces error
txen,txdis,rxen,rxdis call enable/disable interrupt	on Tx or Rx queue
recv {integer=serial number} receives packet with specified serial number into RX queue
rxget {integer=serial number} - retrieves packet from Rx queue and AS IF indicates it to the system
      if packet is not available or serial is different - produces error
rxret {integer=serial number} - AS IF the system returns packet with specified serial number
rxrestart {integer=expected value} calls restart operation on Rx queue
      if returned value is not equal to parameter - produces error
control.rxmode {integer=mode,integer=0|1}
control.addvlan {integer=vlan}
control.delvlan {integer=vlan}

See test.txt for examples
