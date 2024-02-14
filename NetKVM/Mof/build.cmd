mofcomp.exe -B:.\netkvm.bmf ..\Common\netkvm.mof
wmimofck.exe -h..\Common\tmpmof.h -m -u .\netkvm.bmf
fc ..\Common\netkvmmof.h ..\Common\tmpmof.h > nul
if not errorlevel 1 goto thesame
echo Updating netkvmmof.h
copy /Y ..\Common\tmpmof.h ..\Common\netkvmmof.h
:thesame
del ..\Common\tmpmof.h
