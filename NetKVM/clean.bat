rmdir /S /Q win7

rmdir /S /Q wlh\objfre_wlh_x86
rmdir /S /Q wlh\objfre_wlh_amd64
rmdir /S /Q wlh\objfre_win7_amd64
rmdir /S /Q wlh\objfre_win7_x86
del wlh\makefile
del wlh\BuildLog.htm

rmdir /S /Q wxp\objfre_wnet_amd64
rmdir /S /Q wxp\objfre_w2k_x86
rmdir /S /Q wxp\objfre_wxp_x86
del wxp\makefile
del wxp\BuildLog.htm

rmdir /S /Q virtio\objfre_wlh_x86
rmdir /S /Q virtio\objfre_wnet_amd64
rmdir /S /Q virtio\objfre_w2k_x86
rmdir /S /Q virtio\objfre_wxp_x86
rmdir /S /Q virtio\objfre_wlh_amd64
rmdir /S /Q virtio\objfre_win7_amd64
rmdir /S /Q virtio\objfre_win7_x86

rmdir /S /Q common\objfre_wlh_x86
rmdir /S /Q common\objfre_wnet_amd64
rmdir /S /Q common\objfre_w2k_x86
rmdir /S /Q common\objfre_wxp_x86
rmdir /S /Q common\objfre_wlh_amd64
rmdir /S /Q common\objfre_win7_amd64
rmdir /S /Q common\objfre_win7_x86
del common\makefile

rmdir /S /Q Install

del VirtIO\*.c VirtIO\*.h VirtIO\makefile
for /d %%d in  (VirtIO\fre*) do rmdir /S /Q %%d

del /F *.log *.wrn
del dirs

pushd CoInstaller
call clean.bat
popd

