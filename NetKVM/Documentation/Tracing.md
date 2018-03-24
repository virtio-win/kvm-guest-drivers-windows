# NetKVM Tracing #

NetKVM supports both "WPP" tracing and "kdprint" tracing. By default the WPP
tracing is enabled and can be viewed using TraceView as documented below. "kdprint"
tracing can be viewed by using Dbgview.

Viewing WPP tracing

* By default, TraceView.exe is located in the tools\tracing\<Platform> subdirectory
  of the Windows Driver Kit (WDK), where <Platform> is either i386, amd64, or ia64.
* Run TraceView.exe as Administrator and Choose "File"->"Create New Log Session".
* Click "Add Provider" and from the Windows that shows up choose PDB, provide
  the suitable pdb file and click "OK".
* Click "Next" twice and the session should be started.

Choosing Tracing Level

This can be easily done from the driver's advanced tab in device manager by
choosing the desired level for Logging.Level. This configuration holds for both
"WPP" and "kdprint"

Disabling WPP tracing

Disabling the WPP tracing and using plain "kdprint" can be done by simply commenting
"#define NETKVM_WPP_ENABLED" from the Trace.h file.

* Note that in NetKVM WPP is supported only from Win7 and higher
