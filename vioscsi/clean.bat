@echo on

rmdir /S /Q .\Install

rmdir /S /Q objchk_wlh_x86
rmdir /S /Q objchk_wlh_amd64
rmdir /S /Q objchk_win7_x86
rmdir /S /Q objchk_win7_amd64
rmdir /S /Q objfre_wlh_x86
rmdir /S /Q objfre_wlh_amd64
rmdir /S /Q objfre_win7_x86
rmdir /S /Q objfre_win7_amd64
rmdir /S /Q objfre_win8_x86
rmdir /S /Q objfre_win8_amd64
rmdir /S /Q .\sdv
rmdir /S /Q .\sdv.temp

del /F *.log *.wrn *.err
del vioscsi-2012.h
del vioscsi.dvl.xml
del sdv-map.h


