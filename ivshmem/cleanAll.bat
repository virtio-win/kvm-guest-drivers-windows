@echo on

rmdir /S /Q Install
call ..\build\clean.bat

pushd test
call cleanAll.bat
popd
