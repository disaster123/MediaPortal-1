@ECHO OFF

rem build init
set project=TVServer_Client
call BuildInit.bat %1

rem build
echo.
echo Writing GIT revision assemblies...
rem %DeployVersionGIT% /git="%GIT_ROOT%" /path="%TVLibrary%" >> %log%
%DeployVersionGIT% /git="%GIT_ROOT%" /path="%CommonMPTV%" >> %log%

IF NOT EXIST "%TVLibrary%\TVDatabase\EntityModel\App.Config.buildbackup" move "%TVLibrary%\TVDatabase\EntityModel\App.Config" "%TVLibrary%\TVDatabase\EntityModel\App.Config.buildbackup"
IF NOT EXIST "%TVLibrary%\TVDatabase\EntityModel\Model.edmx.buildbackup" move "%TVLibrary%\TVDatabase\EntityModel\Model.edmx" "%TVLibrary%\TVDatabase\EntityModel\Model.edmx.buildbackup"
IF NOT EXIST "%TVLibrary%\TVDatabase\EntityModel\Model.edmx.sql.buildbackup" move "%TVLibrary%\TVDatabase\EntityModel\Model.edmx.sql" "%TVLibrary%\TVDatabase\EntityModel\Model.edmx.sql.buildbackup"
IF NOT EXIST "%TVLibrary%\TvService\App.config.buildbackup" move "%TVLibrary%\TvService\App.config" "%TVLibrary%\TvService\App.config.buildbackup"

copy "%TVLibrary%\TVDatabase\EntityModel\App_MySQL.Config"	"%TVLibrary%\TVDatabase\EntityModel\App.Config"
copy "%TVLibrary%\TVDatabase\EntityModel\Model_MySQL.edmx" "%TVLibrary%\TVDatabase\EntityModel\Model.edmx"
copy "%TVLibrary%\TVDatabase\EntityModel\Model_MySQL.edmx.sql" "%TVLibrary%\TVDatabase\EntityModel\Model.edmx.sql"
copy "%TVLibrary%\TvService\App_MySQL.config" "%TVLibrary%\TvService\App.config"

echo.
echo Building TV Server...
"%WINDIR%\Microsoft.NET\Framework\v4.0.30319\MSBUILD.exe" /target:Rebuild /property:Configuration=%BUILD_TYPE%;Platform=x86 "%TVLibrary%\Mediaportal.TV.Server.sln" >> %log%
rem echo.
rem echo Building TV Client plugin...
rem "%WINDIR%\Microsoft.NET\Framework\v4.0.30319\MSBUILD.exe" /target:Rebuild /property:Configuration=%BUILD_TYPE%;Platform=x86 "%TVLibrary%\TvPlugin\TvPlugin.sln" >> %log%

move "%TVLibrary%\TVDatabase\EntityModel\App.Config.buildbackup" "%TVLibrary%\TVDatabase\EntityModel\App.Config"
move "%TVLibrary%\TVDatabase\EntityModel\Model.edmx.buildbackup" "%TVLibrary%\TVDatabase\EntityModel\Model.edmx"
move "%TVLibrary%\TVDatabase\EntityModel\Model.edmx.sql.buildbackup" "%TVLibrary%\TVDatabase\EntityModel\Model.edmx.sql"
move "%TVLibrary%\TvService\App.config.buildbackup" "%TVLibrary%\TvService\App.config"

echo.
echo Reverting assemblies...
rem %DeployVersionGIT% /git="%GIT_ROOT%" /path="%TVLibrary%" /revert >> %log%
%DeployVersionGIT% /git="%GIT_ROOT%" /path="%CommonMPTV%" /revert >> %log%

echo.
echo Reading the GIT revision...
%DeployVersionGIT% /git="%GIT_ROOT%" /path="%CommonMPTV%" /GetVersion >> %log%
rem SET /p version=<version.txt >> %log%
SET version=%errorlevel%
DEL version.txt >> %log%

echo.
echo Building Installer...
"%progpath%\NSIS\makensis.exe" /DBUILD_TYPE=%BUILD_TYPE% /DVER_BUILD=%version% "%TVLibrary%\Setup\setup.nsi" >> %log%
