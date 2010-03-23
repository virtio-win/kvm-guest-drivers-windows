MessageIdTypedef=NTSTATUS

SeverityNames=(Success=0x0:STATUS_SEVERITY_SUCCESS
               Informational=0x1:STATUS_SEVERITY_INFORMATIONAL
               Warning=0x2:STATUS_SEVERITY_WARNING
               Error=0x3:STATUS_SEVERITY_ERROR
              )

FacilityNames=(System=0x0
               RpcRuntime=0x2:FACILITY_RPC_RUNTIME
               RpcStubs=0x3:FACILITY_RPC_STUBS
               Io=0x4:FACILITY_IO_ERROR_CODE
               Balloon=0x6:FACILITY_BALLOON_ERROR_CODE
              )


MessageId=0x0001 Facility=Balloon Severity=Error SymbolicName=BALLOON_RESOURCES_NOT_FOUND
Language=English
The kernel debugger is already using.
.

MessageId=0x0002 Facility=Balloon Severity=Error SymbolicName=BALLOON_NO_BUFFER_ALLOCATED
Language=English
No memory could be allocated.
.

MessageId=0x0003 Facility=Balloon Severity=Informational SymbolicName=BALLOON_STARTED
Language=English
Driver was started successfully.
.

MessageId=0x0004 Facility=Balloon Severity=Informational SymbolicName=BALLOON_STOPPED
Language=English
Driver was stopped successfully.
.
