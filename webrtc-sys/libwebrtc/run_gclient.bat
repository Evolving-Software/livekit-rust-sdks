@echo off
set PYTHONPATH=C:\Users\Administrator\AppData\Local\Programs\Python\Python310\Lib\site-packages;%PYTHONPATH%
set DEPOT_TOOLS_WIN_TOOLCHAIN=0
set VPYTHON_BYPASS=manually managed python not supported by chrome operations
%~dp0\depot_tools\python.bat %~dp0\depot_tools\gclient.py sync -D --with_branch_heads --with_tags
