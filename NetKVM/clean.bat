rmdir /S /Q wlh\objfre_wlh_x86
rmdir /S /Q wlh\objfre_wlh_amd64
del wlh\BuildLog.htm

rmdir /S /Q wxp\objfre_wnet_amd64
rmdir /S /Q wxp\objfre_w2k_x86
rmdir /S /Q wxp\objfre_wxp_x86
del wxp\BuildLog.htm

rmdir /S /Q virtio\objfre_wlh_x86
rmdir /S /Q virtio\objfre_wnet_amd64
rmdir /S /Q virtio\objfre_w2k_x86
rmdir /S /Q virtio\objfre_wxp_x86
rmdir /S /Q virtio\objfre_wlh_amd64

rmdir /S /Q common\objfre_wlh_x86
rmdir /S /Q common\objfre_wnet_amd64
rmdir /S /Q common\objfre_w2k_x86
rmdir /S /Q common\objfre_wxp_x86
rmdir /S /Q common\objfre_wlh_amd64

rmdir /S /Q Install

del VirtIO\*.c VirtIO\*.h VirtIO\makefile
for /d %%d in  (VirtIO\fre*) do rmdir /S /Q %%d

del /F *.log *.wrn
del dirs
