CONST ForReading = 1
CONST ForWriting = 2
 
Dim objFSObject, strLineBuffer
 
' Lines 4-5: Assign input file to variable
 
Dim strInputFile, strOutputFile
 
strInputFile = Wscript.Arguments(0)

' 0 - is empty, 1 - non-empty line

LineIsEmpty = 1
 
' Lines 6-7: Set up File System object and read input file
 
Set objFSOObject = CreateObject("Scripting.FileSystemObject")
 
Set objCurrentFile = objFSOObject.OpenTextFile(strInputFile, ForReading)
 
' Processing Loop - Store non-blank lines in temporary buffer
 
Do Until objCurrentFile.AtEndOfStream
 
tempLine = objCurrentFile.Readline
 
tempLine = Trim(tempLIne)
 
If Len(tempLine) > 0 or LineIsEmpty = 0 Then
 
strLineBuffer = strLineBuffer &	tempLine & " " & vbCrLf

LineIsEmpty = (Len(tempLine) = 0)

End If

Loop
 
' Write buffer to input file
 
objCurrentFile.Close
 
' Set objCurrentFile = objFSOObject.CreateTextFile(strOutputFile, ForWriting)
 
' objCurrentFile.Write strLineBuffer

' Console.Write strLineBuffer

WScript.Echo strLineBuffer




