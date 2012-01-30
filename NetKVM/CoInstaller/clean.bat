del BuildLog.htm

rmdir /S /Q objfre_wlh_x86
rmdir /S /Q objfre_wnet_amd64
rmdir /S /Q objfre_w2k_x86
rmdir /S /Q objfre_wxp_x86
rmdir /S /Q objfre_wlh_amd64
rmdir /S /Q objfre_win7_amd64
rmdir /S /Q objfre_win7_x86

rmdir /S /Q "Debug unicode"
rmdir /S /Q "Release unicode"
rmdir /S /Q "Debug MBCS"
rmdir /S /Q "Release MBCS"
rmdir /S /Q x64

del /F *.log *.wrn *.err *.aps

del makefile
