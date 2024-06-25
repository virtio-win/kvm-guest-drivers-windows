@echo on

rmdir /S /Q Install
call ..\Tools\clean.bat

pushd test
call cleanAll.bat
popd
