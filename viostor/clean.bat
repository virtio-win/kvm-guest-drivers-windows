@echo on

rmdir /S /Q .\Install

rmdir /S /Q objfre_wxp_x86
rmdir /S /Q objfre_wnet_x86
rmdir /S /Q objfre_wnet_amd64
rmdir /S /Q objfre_wlh_x86
rmdir /S /Q objfre_wlh_amd64
rmdir /S /Q objfre_win7_x86
rmdir /S /Q objfre_win7_amd64
rmdir /S /Q objfre_win8_x86
rmdir /S /Q objfre_win8_amd64
rmdir /S /Q .\sdv
rmdir /S /Q .\sdv.temp

del /F *.log *.wrn *.err *.sdf
del viostor-2012.h
del viostor.dvl.xml
del sdv-map.h

