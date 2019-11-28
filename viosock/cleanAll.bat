@echo on

rmdir /S /Q Install

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
