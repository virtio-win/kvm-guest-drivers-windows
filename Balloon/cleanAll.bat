@echo on

rmdir /S /Q Install

del /F *.log *.wrn *.err

cd app
call cleanAll.bat
cd ..

cd sys
call cleanAll.bat
cd ..

