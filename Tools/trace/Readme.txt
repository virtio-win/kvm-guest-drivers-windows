This directory contains utilities for collecting traces from virtio-win
kernel drivers (release builds). The drivers use Windows Event Tracing,
so in order to make them to provide traces we Windows logger utility.

Preparation:
- Download entire 'trace' directory to the writable location on the VM
  (assume this is Desktop\trace directory)

Typical usage (collecting traces including driver initialization):
- Disable the driver of the interest via Device Manager
- Open administrator command prompt
- pushd Desktop\trace
- Run batch file for respective driver, for example "netkvm.bat", you will see:
     "Recording started"
     "Reproduce the problem, then press ENTER"
- Enable the driver of the interest in the device Manager
- When you decide to stop the tracing - press ENTER in the command-line window of the trace
- Wait until the script finishes working, you will see:
     "Please collect <drivername>.ETL file now"
- The ETL file is a binary record. If you do not have the PDB file for the
  installed build of the driver, you can submit this ETL file to the
  problem report and provide the version of the target driver.
  
You can also decode this ETL file to see a human-readable data. For that you need:
- an exact PDB file (typically it is on the installation media in the same directory as
  the installed driver)
- two utilities from WDK installation: tracefmt.exe and tracepdb.exe
  They are usually under C:\Program Files (x86)\Windows Kits\10\bin\...\x64
  Place both utilities and the PDB file to the same "Desktop\trace" directory
- From the same command line window type:
  ParseTrace.bat <ETLFileName> <PDBFileName>

For getting driver traces from the system boot, refer
https://learn.microsoft.com/en-us/windows/win32/etw/configuring-and-starting-an-autologger-session
