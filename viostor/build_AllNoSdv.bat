@echo off
setlocal
SET _BUILD_DISABLE_SDV=Yes
call buildAll.bat %*
