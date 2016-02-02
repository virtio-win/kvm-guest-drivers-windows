@echo on

rmdir /S /Q Install

del /F *.log *.wrn *.err

cd sys
call cleanAll.bat
cd ..

