' $1 - Visual studio version to run (10 or 11)
' $2 ... Parameters to pass

Dim strCmdLine, strTemp
Set WshShell = Wscript.CreateObject("Wscript.Shell")
strCmdLine = WshShell.RegRead("HKLM\SOFTWARE\Wow6432Node\Microsoft\VisualStudio\" + Wscript.Arguments(0) + ".0\InstallDir")
strCmdLine = chr(34) + strCmdLine + "devenv.com" + chr(34)
For i = 0 to (Wscript.Arguments.Count - 1)
  If i > 0 Then
  	strTemp = Wscript.Arguments(i)
  	If InStr(strTemp, " ") Or InStr(strTemp, "|") Then
  		strCmdLine = strCmdLine + " " + chr(34) + strTemp + chr(34)
  	Else
  		strCmdLine = strCmdLine + " " + strTemp
  	End If
  End If
Next

WScript.Echo "Running " + strCmdLine + vbCrLf
errorLevel = WshShell.Run(strCmdLine, 1, true)
Wscript.Quit errorLevel


