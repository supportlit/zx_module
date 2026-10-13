@echo off

if "%1"=="-h" (
    echo parameters help:
    echo                     build project
    echo     [-t target]     target select, default target=hello
    echo     [-c]            clean project
    echo     [-d]            download
    echo     [-fs]           set flash size=[size]MB, default is 16MB, option is 8MB
    echo    [-bv version]            set module version:ZX600CG/ZX600RG/ZX800RG/ZX800SG
    echo    [-cn customer name]      set customer name: Goly
    exit /B 0
)

set folder_ab_current=%cd%
set para_ab_cmd=build
set para_ab_target=soundbox
set para_flash_size=4
set para_module_version=ZX800SG
set VFP_EN=y
set RELEASE_MODE=NON_EBABLE
set customer_name=default

::GAIN_V1=y Enable v1, use 3w gain
set GAIN_V1=n
set PKT_EN=n
set HARDWARE=LIT4GCN

set folder_ab_build_out=%folder_ab_current%\out
set file_ab_build_config=%folder_ab_build_out%\bcfg 
set ctool_exec=%folder_ab_current%\..\Tools\ctools.exe
set cmd_ab_python=%folder_ab_current%\prebuilts\win32\python3\python3.exe

::load build config
if exist %file_ab_build_config% (
    for /f "tokens=1,2 delims='" %%a in (%file_ab_build_config%) do (
        if "%%a"=="target=" set para_ab_target=%%b
        if "%%a"=="flash=" set para_flash_size=%%b
        if "%%a"=="module_version=" set para_module_version=%%b
        if "%%a"=="gain_v1=" set GAIN_V1=%%b
        if "%%a"=="vfp_enable=" set VFP_EN=%%b
        if "%%a"=="customer_name=" set customer_name=%%b
    )
)

::parse parameters
goto para_ab_parse
:para_ab_parse
if not "%1"=="" (
    if "%1"=="-c" (
        set para_ab_cmd=clean
    ) else if "%1"=="-d" (
        set para_ab_cmd=download
    ) else if "%1"=="-t" (
        set para_ab_target=%2
        shift /0
    ) else if "%1"=="-fs" (
        set para_flash_size=%2
        shift /0
    )  else if "%1"=="-v1" (
        set GAIN_V1=y
        shift /0
    ) else if "%1"=="-pkt" (
    	set PKT_EN=y
        set HARDWARE=LIT4GPKT
        shift /0
    ) else if "%1"=="-bv" (
        set para_module_version=%2
        shift /0
    ) else if "%1"=="-r" (
        set RELEASE_MODE=ENABLE_RELEASE
        shift /0
    ) else if "%1"=="-cn" (
        set customer_name=%2
        shift /0
    ) else (
        echo unknown para %1
        exit /B 1
    )

    shift /0
    goto para_ab_parse
)

echo ****************************************
echo cmd=%para_ab_cmd%
echo target=%para_ab_target%
echo module version=%para_module_version%
echo HARDWARE=%HARDWARE%
echo customer name=%customer_name%
echo ****************************************

::check module version
@REM set module_kernel_path=%folder_ab_current%\..\kernel\%para_module_version%\
@REM echo %module_kernel_path%
@REM if not exist %module_kernel_path% (
@REM     echo module select error
@REM     exit /B 1
@REM )

::save previous build parameters
if not exist %folder_ab_build_out% (
    md %folder_ab_build_out%
)
echo target='%para_ab_target%'>%file_ab_build_config%
echo flash='%para_flash_size%'>>%file_ab_build_config%
echo module_version='%para_module_version%'>>%file_ab_build_config%
echo gain_v1='%GAIN_V1%'>>%file_ab_build_config%
echo vfp_enable='%VFP_EN%'>>%file_ab_build_config%
echo customer_name='%customer_name%'>>%file_ab_build_config%

set module_version=%para_module_version%
if %para_module_version% == ZX600RG (
    if %para_flash_size% == 8 (
        set module_version=%para_module_version%_SA
    ) else if %para_flash_size% == 4 (
        set module_version=%para_module_version%_CA
    ) else (
        echo "ZX600RG flash size only support 8M and 4M"
        exit /B 1
    )
) else if %para_module_version% == ZX800RG (
    if %para_flash_size% == 8 (
        set module_version=%para_module_version%_SA
    ) else if %para_flash_size% == 4 (
        set module_version=%para_module_version%_CA
    ) else (
        echo "ZX600RG flash size only support 8M and 4M"
        exit /B 1
    )
) else if %para_module_version% == ZX800SG (
    if %para_flash_size% == 2 (
        set module_version=%para_module_version%_CA
    ) else if %para_flash_size% == 4 (
        set module_version=%para_module_version%_SA
    ) else (
        echo "ZX800SG flash size only support 4M and 2M"
        exit /B 1
    )
) else if %para_module_version% == ZX800VG (
    if %para_flash_size% == 2 (
        set module_version=%para_module_version%_CA
        set VFP_EN=n
    ) else if %para_flash_size% == 4 (
        set module_version=%para_module_version%_SA
    ) else (
        echo "ZX800VG flash size only support 4M and 2M"
        exit /B 1
    )
) else (
  echo "no this version!"
        exit /B 1
)

::check para_ab_cmd
if "%para_ab_cmd%"=="clean" (
    call :clean
) else if "%para_ab_cmd%"=="build" (
    call :build
) else if "%para_ab_cmd%"=="download" (
    call :download
) else (
    echo error para_ab_cmd!!!
    exit /B 1
)



exit /B 0

:build
    echo build target: %para_ab_target%

    @REM if "%para_ab_target%"=="hello" (
    @REM     copy /y .\CMakeLists.txt.in .\CMakeLists.txt
    @REM     call :_build
    @REM     goto :EOF
    @REM ) 

    set para_ab_example_name=%para_ab_target%
    set para_ab_example_suffix=
    if exist .\examples\%para_ab_example_name%.c (
        set para_ab_example_suffix=c
    ) else if exist .\examples\%para_ab_example_name%.cpp (
        set para_ab_example_suffix=cpp
    ) 

    if "%para_ab_example_suffix%"=="" (
        echo example %para_ab_example_name% not exist!
        exit /B 1
    )

    echo build %para_ab_example_name%.%para_ab_example_suffix%
    
    call :_build
    goto :EOF


:_build
    call .\tools\core_launch.bat
    cd %folder_ab_build_out%\appimage_debug
    cmake ..\.. -G Ninja
    ninja
    cd ..\..

    if exist ..\kernel\ (
        xcopy /Q /Y .\out\appimage_debug\hex\customer_app.bin  ..\kernel\%module_version%
    )

    goto :EOF


:clean
    echo clean 
    if exist %folder_ab_build_out% (
        echo rmdir %folder_ab_build_out%
        rmdir /s /q %folder_ab_build_out%
    )
    goto :EOF

:download
    call download.bat -app
    goto :EOF
