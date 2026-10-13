@echo off
REM ========================================
REM  SoundBox SDK Build Script (Framework Version)
REM  Based on build.bat implementation
REM ========================================

REM Debug: Show first argument
REM echo DEBUG: First argument is: "%1"

if "%1"=="-h" goto show_help
if "%1"=="" goto show_help

goto start_build

:show_help
    echo Usage:
    echo     build.bat [customer_name] [-p product_model] [-fs flash_size]  Build project for customer
    echo     build.bat -c [customer_name]                                    Clean project
    echo     build.bat -d                                                    Download firmware
        echo     -r                                                    Build release (USB log output)
    echo     build.bat -h                                                    Show this help
    echo.
    echo Options:
    echo     -p product_model    Specify product model (e.g., SB100, PX200)
    echo                         If not specified, will load from product.ini
    echo     -fs flash_size      Set flash size in MB (2, 4, 8, 16), default is 4MB
    echo.
    echo Example:
    echo     build.bat template                Build with auto-detected product model
    echo     build.bat template -p SB100       Build with specific product model
    echo     build.bat template -fs 2          Build with 2MB flash
    echo     build.bat -c template             Clean project
    echo.
    echo Available customers:
    if exist customers dir /b customers
    exit /B 0

:start_build
REM Parse parameters
set para_ab_cmd=build
set CUSTOMER_NAME=
set PRODUCT_MODEL=SB100
set para_flash_size=4
set para_module_version=ZX800VG
set VFP_EN=n
set GAIN_V1=n
set PKT_EN=n
set HARDWARE=LIT4GCN
set RELEASE_MODE=DISABLE_RELEASE
set BUILD_TARGET=appimage
set BUILD_RELEASE_TYPE=debug
set HW_EXT_FLASH_TYPE=NOR
set HW_EXT_FLASH_SIZE=0
set HW_EXT_FLASH_NUM=1

REM Get current directory
set folder_ab_current=%cd%
set folder_ab_build_out=%folder_ab_current%\out
set file_ab_build_config=%folder_ab_build_out%\bcfg
set cmd_ab_python=%folder_ab_current%\prebuilts\win32\python3\python3.exe
set folder_ab_kernel_default=%folder_ab_current%\..\kernel
set folder_ab_kernel_base=%folder_ab_kernel_default%

REM Parse command line arguments
:para_parse
if not "%1"=="" (
    if "%1"=="-c" (
        set para_ab_cmd=clean
        set CUSTOMER_NAME=%2
        shift /0
        shift /0
    ) else if "%1"=="-d" (
        set para_ab_cmd=download
        shift /0
    ) else if "%1"=="-p" (
        set PRODUCT_MODEL=%2
        shift /0
        shift /0
    ) else if "%1"=="-fs" (
        set para_flash_size=%2
        shift /0
        shift /0
    ) else if "%1"=="-r" (
        set BUILD_RELEASE_TYPE=release
        set RELEASE_MODE=ENABLE_RELEASE
        shift /0
    ) else (
        set CUSTOMER_NAME=%1
        shift /0
    )
    goto para_parse
)

REM Resolve build output folder based on target/type
set BUILD_FOLDER=%BUILD_TARGET%_%BUILD_RELEASE_TYPE%
set folder_ab_build_dir=%folder_ab_build_out%\%BUILD_FOLDER%
set folder_ab_hex_dir=.\out\%BUILD_FOLDER%\hex

REM If download command, execute directly
if "%para_ab_cmd%"=="download" (
    call download.bat -app
    exit /B 0
)

REM Check customer name
if "%CUSTOMER_NAME%"=="" (
    echo Error: Customer name is required
    echo Usage: build_new.bat [customer_name]
    echo Example: build_new.bat template
    echo.
    echo Available customers:
    if exist customers dir /b customers
    exit /B 1
)

REM Check customer directory
if not exist "customers\%CUSTOMER_NAME%" (
    echo Error: Customer directory not found: customers\%CUSTOMER_NAME%
    echo.
    echo Available customers:
    if exist customers dir /b customers
    exit /B 1
)

REM Compute kernel path after customer name is known
set folder_ab_kernel_customer=%folder_ab_current%\customers\%CUSTOMER_NAME%\kernel
if exist "%folder_ab_kernel_customer%" (
    set folder_ab_kernel_base=%folder_ab_kernel_customer%
)
set KERNEL_BASE=%folder_ab_kernel_base%

REM Load previous build configuration if exists
if exist "%file_ab_build_config%" (
    setlocal enabledelayedexpansion
    echo Loading previous build configuration...
    for /f "tokens=1,2 delims==" %%a in ('type "%file_ab_build_config%"') do (
        if "%%a"=="product_model" (
            REM Remove quotes from value
            set temp_val=%%b
            set temp_val=!temp_val:'=!
            set SAVED_PRODUCT_MODEL=!temp_val!
        )
        if "%%a"=="flash" (
            REM Remove quotes from value
            set temp_val=%%b
            set temp_val=!temp_val:'=!
            set SAVED_FLASH_SIZE=!temp_val!
        )
        if "%%a"=="HW_EXT_FLASH_TYPE" (
            set temp_val=%%b
            set temp_val=!temp_val:'=!
            set SAVED_EXT_FLASH_TYPE=!temp_val!
        )
        if "%%a"=="HW_EXT_FLASH_SIZE" (
            set temp_val=%%b
            set temp_val=!temp_val:'=!
            set SAVED_EXT_FLASH_SIZE=!temp_val!
        )
        if "%%a"=="HW_EXT_FLASH_NUM" (
            set temp_val=%%b
            set temp_val=!temp_val:'=!
            set SAVED_EXT_FLASH_NUM=!temp_val!
        )
    )
    endlocal & (
        set SAVED_PRODUCT_MODEL=%SAVED_PRODUCT_MODEL%
        set SAVED_FLASH_SIZE=%SAVED_FLASH_SIZE%
        set SAVED_EXT_FLASH_TYPE=%SAVED_EXT_FLASH_TYPE%
        set SAVED_EXT_FLASH_SIZE=%SAVED_EXT_FLASH_SIZE%
        set SAVED_EXT_FLASH_NUM=%SAVED_EXT_FLASH_NUM%
    )
    if defined SAVED_PRODUCT_MODEL (
        if "%PRODUCT_MODEL%"=="SB100" (
            set PRODUCT_MODEL=%SAVED_PRODUCT_MODEL%
            echo   Using saved product model: %SAVED_PRODUCT_MODEL%
        )
    )
    if defined SAVED_FLASH_SIZE (
        if "%para_flash_size%"=="4" (
            set para_flash_size=%SAVED_FLASH_SIZE%
            echo   Using saved flash size: %SAVED_FLASH_SIZE%MB
        )
    )
    if defined SAVED_EXT_FLASH_TYPE (
        set HW_EXT_FLASH_TYPE=%SAVED_EXT_FLASH_TYPE%
        echo   Using saved external flash type: %HW_EXT_FLASH_TYPE%
    )
    if defined SAVED_EXT_FLASH_SIZE (
        set HW_EXT_FLASH_SIZE=%SAVED_EXT_FLASH_SIZE%
        echo   Using saved external flash size: %HW_EXT_FLASH_SIZE% bytes
    )
    if defined SAVED_EXT_FLASH_NUM (
        set HW_EXT_FLASH_NUM=%SAVED_EXT_FLASH_NUM%
        echo   Using saved external flash num: %HW_EXT_FLASH_NUM%
    )
)

REM Generate sys_cfg.bin from CSV (primary source for external flash type)
if "%HW_EXT_FLASH_SIZE%"=="" set HW_EXT_FLASH_SIZE=0
if "%HW_EXT_FLASH_NUM%"=="" set HW_EXT_FLASH_NUM=1
if "%HW_EXT_FLASH_SIZE%"=="0" set /a HW_EXT_FLASH_SIZE=%para_flash_size%*1024*1024
if "%HW_EXT_FLASH_NUM%"=="0" set HW_EXT_FLASH_NUM=1

set file_ab_sys_cfg_csv=%folder_ab_current%\customers\%CUSTOMER_NAME%\sys_cfg.csv
set file_ab_sys_cfg_bin=%folder_ab_build_out%\sys_cfg.bin
if exist "%file_ab_sys_cfg_csv%" (
    echo Generating sys_cfg.bin from %file_ab_sys_cfg_csv%
    for /f "usebackq tokens=1,2 delims==" %%i in (`%cmd_ab_python% tools/gen_sys_cfg.py --csv "%file_ab_sys_cfg_csv%" --out "%file_ab_sys_cfg_bin%" --print-type --ext-flash-size "%HW_EXT_FLASH_SIZE%" --ext-flash-num "%HW_EXT_FLASH_NUM%"`) do (
        if /I "%%i"=="EXT_FLASH_TYPE" set HW_EXT_FLASH_TYPE=%%j
        if /I "%%i"=="EXT_FLASH_SIZE" set HW_EXT_FLASH_SIZE=%%j
        if /I "%%i"=="EXT_FLASH_NUM" set HW_EXT_FLASH_NUM=%%j
    )
    if errorlevel 1 (
        echo Error: sys_cfg generation failed
        exit /B 1
    )
) else (
    echo WARNING: sys_cfg.csv not found for %CUSTOMER_NAME%, generating default sys_cfg.bin (NOR)
    %cmd_ab_python% tools/gen_sys_cfg.py --out "%file_ab_sys_cfg_bin%" --ext-flash-type "%HW_EXT_FLASH_TYPE%" --ext-flash-size "%HW_EXT_FLASH_SIZE%" --ext-flash-num "%HW_EXT_FLASH_NUM%"
    if errorlevel 1 (
        echo Error: sys_cfg generation failed
        exit /B 1
    )
)

REM Generate version header file
echo Generating version header...
%cmd_ab_python% tools\gen_version.py -c %CUSTOMER_NAME% -p %PRODUCT_MODEL% --sdk-root=.
if errorlevel 1 (
    echo Warning: Failed to generate version header
)
if /I "%BUILD_RELEASE_TYPE%"=="release" (
    call :check_release_dirty
)
echo.

echo ========================================
echo   Building SoundBox Framework
echo ========================================
echo Customer:        %CUSTOMER_NAME%
echo Product Model:   %PRODUCT_MODEL%
echo Command:         %para_ab_cmd%
echo Module Version:  %para_module_version%
echo Build Type:      %BUILD_RELEASE_TYPE%
echo Flash Size:      %para_flash_size%MB
echo Ext Flash:       type=%HW_EXT_FLASH_TYPE%, size=%HW_EXT_FLASH_SIZE% bytes, num=%HW_EXT_FLASH_NUM%
echo Hardware:        %HARDWARE%
if /I "%BUILD_RELEASE_TYPE%"=="release" (
    echo Log Output:      USB ^(LOG_OUTPUT_USB^)
)
echo ========================================
echo.

REM Save build configuration
if not exist %folder_ab_build_out% (
    md %folder_ab_build_out%
)
echo target='soundbox'>%file_ab_build_config%
echo flash='%para_flash_size%'>>%file_ab_build_config%
echo module_version='%para_module_version%'>>%file_ab_build_config%
echo gain_v1='%GAIN_V1%'>>%file_ab_build_config%
echo vfp_enable='%VFP_EN%'>>%file_ab_build_config%
echo customer_name='%CUSTOMER_NAME%'>>%file_ab_build_config%
echo product_model='%PRODUCT_MODEL%'>>%file_ab_build_config%
echo build_release_type='%BUILD_RELEASE_TYPE%'>>%file_ab_build_config%
echo HW_EXT_FLASH_TYPE='%HW_EXT_FLASH_TYPE%'>>%file_ab_build_config%
echo HW_EXT_FLASH_SIZE='%HW_EXT_FLASH_SIZE%'>>%file_ab_build_config%
echo HW_EXT_FLASH_NUM='%HW_EXT_FLASH_NUM%'>>%file_ab_build_config%

REM Set module version based on flash size
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
        echo "ZX800RG flash size only support 8M and 4M"
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
    echo "Unknown module version: %para_module_version%"
    exit /B 1
)

REM 执行命令
if "%para_ab_cmd%"=="clean" (
    call :clean
) else if "%para_ab_cmd%"=="build" (
    call :build
) else (
    echo Error: Unknown command %para_ab_cmd%
    exit /B 1
)

exit /B 0

REM ========================================
REM Release build safety checks
REM ========================================
:check_release_dirty
    set GIT_DIRTY_COUNT=
    for /f %%i in ('git status --porcelain 2^>nul ^| find /c /v ""') do set GIT_DIRTY_COUNT=%%i
    if "%GIT_DIRTY_COUNT%"=="" set GIT_DIRTY_COUNT=0
    if "%GIT_DIRTY_COUNT%"=="0" goto :EOF

    echo.
    echo WARNING: Git workspace has %GIT_DIRTY_COUNT% dirty file^(s^). Version string will include -dirty.
    choice /C YN /M "Continue release build with dirty workspace?"
    if errorlevel 2 (
        echo Release build cancelled by user.
        exit /B 1
    )
    echo Continuing release build with dirty workspace.
    goto :EOF

REM ========================================
REM 构建函数
REM ========================================
:build
    echo Building for customer: %CUSTOMER_NAME%
    echo.

    REM 启动核心构建工具 (参照 build.bat)
    call .\tools\core_launch.bat
    
    REM 进入构建目录
    cd %folder_ab_build_dir%
    
    REM 运行 CMake 配置
    echo Running CMake configuration...
    cmake ..\.. -G Ninja -DCUSTOMER_NAME=%CUSTOMER_NAME%
    
    if errorlevel 1 (
        echo.
        echo Error: CMake configuration failed
        cd ..\..
        exit /B 1
    )
    
    REM 编译
    echo.
    echo Building with Ninja...
    ninja
    
    if errorlevel 1 (
        echo.
        echo Error: Build failed
        cd ..\..
        exit /B 1
    )
    
    REM 返回根目录
    cd ..\..
    
    REM 复制输出文件到kernel目录 (参照 build.bat)
    if exist ..\kernel\ (
        echo.
        echo Copying output to kernel directory...
            if not exist "%KERNEL_BASE%\%module_version%" md "%KERNEL_BASE%\%module_version%"
            xcopy /Q /Y "%folder_ab_hex_dir%\customer_app.bin" "%KERNEL_BASE%\%module_version%\"
            if exist "%file_ab_sys_cfg_bin%" (
                xcopy /Q /Y "%file_ab_sys_cfg_bin%" "%KERNEL_BASE%\%module_version%\" >nul
            )
    )
    
    REM 创建客户输出目录
    set OUTPUT_DIR=output_%CUSTOMER_NAME%
    if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
    
    REM 复制输出文件
    echo Copying output files to %OUTPUT_DIR%...
    if exist "%folder_ab_hex_dir%\*.bin" (
        copy /Y "%folder_ab_hex_dir%\*.bin" "%OUTPUT_DIR%\"
    )
    if exist "%folder_ab_hex_dir%\*.pac" (
        copy /Y "%folder_ab_hex_dir%\*.pac" "%OUTPUT_DIR%\"
    )
    
    REM 复制客户资源文件
    if exist "customers\%CUSTOMER_NAME%\resources" (
        echo Copying customer resources...
        xcopy /s /y "customers\%CUSTOMER_NAME%\resources\*" "%OUTPUT_DIR%\resources\"
    )
    
    echo.
    echo ========================================
    echo   Build completed successfully!
    echo   Output: %OUTPUT_DIR%
    echo   Kernel: ..\kernel\%module_version%
    echo ========================================
    echo.
    
    goto :EOF

REM ========================================
REM 清理函数
REM ========================================
:clean
    echo Cleaning build files for %CUSTOMER_NAME%...
    
    if exist %folder_ab_build_out% (
        echo Removing %folder_ab_build_out%
        rmdir /s /q %folder_ab_build_out%
    )
    
    set OUTPUT_DIR=output_%CUSTOMER_NAME%
    if exist "%OUTPUT_DIR%" (
        echo Removing %OUTPUT_DIR%
        rmdir /s /q "%OUTPUT_DIR%"
    )
    
    echo.
    echo Clean completed.
    echo.
    
    goto :EOF
