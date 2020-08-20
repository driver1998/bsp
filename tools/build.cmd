@echo off
setlocal enableDelayedExpansion
pushd "%~dp0..\build\bcm2836"

REM Find Visual Studio Installations
for /f "tokens=*" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere" -latest -requires Microsoft.Component.MSBuild -find VC\Auxiliary\Build\vcvarsall.bat') do (
	set vcvarsall="%%i"
)
call !vcvarsall! x86

REM Build drivers
msbuild /p:Configuration=Release /p:Platform=ARM64 /m

REM Copy to output directory
rmdir Output /s /q 2>nul
mkdir Output 2>nul
pushd ARM64\Release
for /f "tokens=*" %%i in ('dir /b *.dll *.sys *.inf *.pdb') do (
	copy %%i ..\..\Output /y > nul
)
popd

pushd Output

REM Sign drivers
inf2cat /os:10_VB_ARM64 /drv:. /uselocaltime
signtool sign /fd SHA256 /f "%~dp0testcert.pfx" /p testcert123 /v *.dll *.sys *.cat *.dll

REM Package
for /f "delims=. tokens=1" %%i in ('dir /b *.inf') do (
	mkdir %%i 2>nul
	move %%i.* %%i >nul
)
move vchiq_arm_kern.* vchiq >nul
move vcos_win32_kern.* vchiq >nul

REM Generate compatibility list
for %%m in (RPi3 RPi4) do (
	for /f "tokens=*" %%i in ('dir /b /a:d') do (
		set supported=1

		if /I [%%m] == [RPi3] (
			if /I [%%i] == [vchiq]    set supported=0
			if /I [%%i] == [rpiuxflt] set supported=0
			if /I [%%i] == [bcmgenet] set supported=0
		)

		if /I [%%m] == [RPi4] (
			if /I [%%i] == [vchiq]                set supported=0
			if /I [%%i] == [RpiLanPropertyChange] set supported=0
		)

		if !supported! == 1 (echo %%i>> %%m.txt)
	)
)

popd
popd

