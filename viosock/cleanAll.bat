@echo on

rmdir /S /Q Install
rmdir /S /Q Install_Debug

del /F *.log *.wrn *.err

pushd lib
call cleanAll.bat
popd

pushd sys
call cleanAll.bat
popd

pushd viosock-test
call cleanAll.bat
popd

pushd viosocklib-test
call cleanAll.bat
popd

pushd "ViosockPackage"
call cleanAll.bat
popd