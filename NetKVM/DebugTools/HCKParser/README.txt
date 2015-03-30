    The hck_seq utility parses the pcap file and verifies the
correctness of the HCK packets order. The utility may be useful for
trouble-shooting the HCK error when HCK complains about missing and/or
out of order packet delivery  by the NetKVM network driver.

     The utility is bases on partial HCK packet
formaet reverse-engineering: it assumes that all HCK packet include
the HCK content marked by "NDIS" prefix, the sequence number can be
found as 16-bit little-endian number in 12th an 13th bytes of the HCK
content and session number is 16-bit little-endian number at offset 22
and 23.

    The utility has the single and mandatory argument, name of pcap file.

    The pcap files may be collected both inside the Windows guests
with wireshark and on host, by listening on the 'tap' networking
device with wireshark or tcpdump.

    Please, notice that when running wireshark inside Windows guest
alongside the HCK requires manual intervention: HCK disables and
enables the networking device, and the wireshark capture to be resumed
manually. Wireshark capture resume is easier when unsaved capture
files confirmation is disabled; with wireshark 1.12.2, it may be
accomplished by selecting Edit->Preferences->User Interface.

    The utility's building requires pcap development library.

