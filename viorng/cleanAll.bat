@echo on

rmdir /S /Q Install

del /F *.log *.wrn *.err

cd viorng
call cleanAll.bat
cd ..

cd cng
cd um
call cleanAll.bat
cd ..
cd ..

