setlocal

if not exist .\Install mkdir .\Install
copy ..\viostor\txtsetup-amd64.oem .\Install\
copy ..\viostor\txtsetup-i386.oem .\Install\
copy ..\viostor\disk1 .\Install\

copy ..\LICENSE .\Install\

endlocal
