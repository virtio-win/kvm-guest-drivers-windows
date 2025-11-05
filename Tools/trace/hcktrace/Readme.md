## Utility for collecting driver logs during automatic tests

### Usage

*hcktrace install*
- creates c:\hcktrace directory and populates required helper binaries
- installs hcktrace service (hcktrace.exe is copies itself to System32)
- starts the service

*hcktrace driver <device name> <driver package path> <logging level>*
- device name: netkvm or other device (aligned with HCK-CI supported devices)
- driver package path: full path to directory that contains $devicename.pdb file
- level: 4 (default, INFO) or above (>4 will significantly increase log files and conversion time)

*hcktrace test <testname>*
- starts ETW recording for specific driver
- the record will be continued after reboot
- testname: may be anything but 'stop' and 'end'

*hcktrace test end*
- stops recording, saves log files, compresses them to c:\hcktrace\zip\some_name.zip
- after that the HCK-CI can move c:\hcktrace\zip directory to workspace
- zip files should be deleted from c:\hcktrace\zip after copy

### Current status
- Only netkvm driver supported

### Notes
- the executable must run as an admin
- for 'install' call the original up-to-date binary
- the service keeps its log in c:\hcktrace\servicelog.txt, collect this file if case of any problem with the tool

### TODO
- Add support for other drivers
- Support clean uninstall to allow smooth running on a dirty system
- Add support for ARM64
