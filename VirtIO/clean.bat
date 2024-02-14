for /d %%x in (objfre_*) do rmdir /S /Q %%x
for /d %%x in (objchk_*) do rmdir /S /Q %%x
rmdir /S /Q x64

del /F *.log *.wrn *.err

cd WDF
call clean.bat
cd ..
