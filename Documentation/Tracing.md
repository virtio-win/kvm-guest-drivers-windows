# WPP Tracing #

## Advantages over KdPrint:

* It can be enabled, disabled and filtered during runtime with minimal overhead by logging in real-time binary messages
* Traces are are automatically included in the crash dumps
* Logs can be easily collected using a script
* Can be very useful for automating manual tests which use log's output
* Time Stamps, Function names and some other useful data can be easily included into log

## Viewing the WPP trace messages in real-time ##

1. Copy the driver's PDB file from the build folder to the target machine.
2. Copy **traceview.exe** from WDK install location on the build machine (C:\Program Files (x86)\Windows Kits\8.1\Tools\x64\traceview.exe) to the target machine.
3. Start traceview as an Administrator.
4. On the **File** menu, click **Create New Log Session**.
5. Click **Add Provider**.
6. Click **PDB (Debug Information) File**, and then choose the .pdb file which came with the driver's build, in the build's folder.
7. Click Next.
8. Enter **Log Session Name**, as you like.
9. To save captured traces to a file, check the **Log Trace Event Data To File** option and set the file name, as you like.
10. Click the **>>** to the right of **Set Flags and Level**.
11. Put the right values, follow the "Obtaining the providers control GUID, enabled flags and level" section.

## Getting Driver Binary Traces using Logman (for sending trace logs to developers) ##

* Copy Tools/Trace subfolder from the root of the source code tree to the target machine
* Run <driver_name>.bat as administrator (netkvm.bat for example)
* Follow instructions
* Collect the etl file

## Converting binary trace file (.etl) to text using .pdb file from a suitable build ##

* Copy tracefmt.exe and tracepdb.exe from WDK install location on build machine
  (usually C:\Program Files (x86)\Windows Kits\8.1\bin\x64\tracefmt.exe)
  to Trace folder on the target machine
* Run the parseTrace.bat script as follows:
  parseTrace.bat <The suitable pdp file for build> <The collected etl file>
* Please note that the tracefmt.exe and tracepdb.exe should be in the same folder
  as the parseTrace.bat.

## Obtaining the providers control GUID enabled flags and level ##

1. Copy the script located in "Tools/getpdbinfo.bat" to any empty folder.
2. Copy **tracepdb.exe** from WDK install location on the build machine (C:\Program Files (x86)\Windows Kits\8.1\bin\x64\tracepdb.exe) to the same folder.
3. Copy the .pdb file which comes with the driver's build to the scripts directory.
4. Open a command window and run, (Change pdbFile to your .pdb file name):
```
getpdbinfo.bat pdbFile
```
5. Read the following to understand how to determine which information is relevant :
   - **Control GUID** value
     The script copies it to the clipboard with braces added.
   - **EnableFlags** value
     Provider-defined value that specifies the class of events for which the provider generates events.
     The value is a 32bit binary word, each bit resembles a flag, each flag has a value and this value is a 32bit word as a hex number in which only 1 bit is on and its the bit's index which resembles the flag, if you pay attention to the .tmc file content you can see the flags and their values.
     In order to enable flags just turn on the bit with the right index in the 32bit word, when done, convert the 32bit word to a hexadecimal number. (its recommended to enable all flags!)
   - **EnableLevel** value
     Provider-defined value that specifies the level of detail included in the event.
     Putting the value to 6, includes all the important message levels.

## How to enable WPP tracing for a component at boot time ##

The AutoLogger session is used to capture traces before the user logs in.

### Configuring an autologger session ###

#### Using the tracelog.exe ####

1. Copy **tracelog.exe** from WDK install location on the build machine (C:\Program Files (x86)\Windows Kits\8.1\bin\x64\tracelog.exe) to the target pc's (C:\Windows\System32) folder.
2. Go to https://www.guidgenerator.com/ and generate a GUID for the logger session.
3. Open any text editor and copy this command line:
```
tracelog -addautologger [LoggerSessionName] -sessionguid #GeneratedGUID -flag Flag -level Level -guid #ProvidersControlGUID
```
4. Change the _[LoggerSessionName]_ to the desired session's name.
5. Change the value of _GeneratedGUID_ to our generated guid accordingly, keep the #.
6. Change the rest of the values: _Flag_, _Level_ and _ProvidersControlGUID_, keep the #, follow the "Obtaining the providers control GUID, enabled flags and level" section.
7. Once done, run the command line with cmd, (as an administrator).
8. Reboot the target pc for the changes to take effect.

#### **manually** Using the registry ####

1. Run regedit.
2. Navigate to "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\WMI\Autologger".
3. Under the "Autologger" key create a key for the AutoLogger session that you want to configure and rename it to the desired session's name.
   - Go to https://www.guidgenerator.com/ and generate a GUID with the braces option enabled, this is for the logger session.
   - Add a new "String Value" to the key and rename it to "GUID", put the generated GUID as the value.
   - Add a new "DWORD Value" to the key and rename it to "Start", put 1 in the value.
   - NOTE: you can add more values to better configure the session refer to: https://msdn.microsoft.com/en-us/library/aa363687.aspx
4. Under the session's key create a key for our provider that you want to enable to the session. Use the driver's provider control GUID as the name of the key. (explained below!)
   - Add a new "DWORD Value" to the key and rename it to "Enabled", put 1 in the value.
   - Add a new "DWORD Value" to the key and rename it to "EnableFlags", put in the enabled flags hex value, follow the "Obtaining the providers control GUID, enabled flags and level" section.
   - Add a new "DWORD Value" to the key and rename it to "EnableLevel", put 6 in the value, follow the "Obtaining the providers control GUID, enabled flags and level" section.
   - NOTE: you can add more values to better configure the provider refer to: https://msdn.microsoft.com/en-us/library/aa363687.aspx
5. Reboot the target pc for the changes to take effect.

Now, with every boot or shutdown the logger is going to create a .etl trace log file to "..\Windows\System32\LogFiles\WMI\<session'sname>.etl".

## Reading .etl log files using TraceView ##

1. Copy **traceview.exe** from WDK install location on the build machine (C:\Program Files (x86)\Windows Kits\8.1\Tools\x64\traceview.exe) to the target machine.
2. Start traceview as an Administrator.
3. On the **File** menu, click **Open Existing Log File**.
4. In the **Log File Name** box, choose the .etl file you want to read.
5. Click **OK**.
6. Click **PDB (Debug Information) File** and then choose the .pdb file which came with the driver's build, in the build's folder.
7. Click **OK**.
