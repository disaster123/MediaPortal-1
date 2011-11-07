@ECHO OFF

# echo DirectShowFilters
# cd DirectShowFilters
# start Filters.sln
# cd ..

# pause

echo.
echo MediaPortal

del Release\Setup-MediaPortal-*.exe

cd Build

call "MSBUILD_Rebuild_Release_MediaPortal.bat"

pause