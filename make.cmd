@echo off
build -sg
copy driver\objchk_win7_amd64\amd64\audionet.sys audionet.sys
copy proppage\objchk_win7_amd64\amd64\audionet_prop.dll audionet_prop.dll