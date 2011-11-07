@ECHO OFF

echo.

echo Tv Server

del Release\Setup-TVServer-*.exe

cd Build

call "MSBUILD_Rebuild_Release_TVServer_Client.bat"

pause