call buildAll.bat PackInstall
if exist Install.iso del Install.iso
copy %_DRIVER_ISO_NAME% Install.iso

