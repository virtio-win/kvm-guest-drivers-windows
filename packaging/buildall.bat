setlocal

mkdir Install
copy ..\viostor\txtsetup-amd64.oem .\Install\
copy ..\viostor\txtsetup-i386.oem .\Install\
copy ..\viostor\disk1 .\Install\

copy ..\COPYING .\Install\
copy ..\LICENSE .\Install\

endlocal