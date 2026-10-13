@echo off
setlocal enabledelayedexpansion
REM ========================================
REM  SoundBox SDK Download Script
REM  Based on build.bat implementation
REM ========================================

set folder_dw_current=%cd%

set folder_out_bin=%folder_dw_current%\out\
set folder_res_bin=%folder_dw_current%\res\

set para_dw_flash_size=[]
set module_version=ZX600CG
set file_app_build_config=%folder_out_bin%\bcfg
set module_cp2=n
set product_model=
set hw_ext_flash_type=NOR
set hw_ext_flash_size=0
set hw_ext_flash_num=1

set folder_dw_kernel_default=%folder_dw_current%\..\kernel
set folder_dw_kernel_base=%folder_dw_kernel_default%

if exist %file_app_build_config% (
    for /f "tokens=1,2 delims='" %%a in (%file_app_build_config%) do (
        if "%%a"=="flash=" (
            set para_dw_flash_size=%%b
        ) 
        if "%%a"=="module_version=" (
            set module_version=%%b
        ) 
        if "%%a"=="flash=" (
            set para_dw_flash_size=%%b
        )
        if "%%a"=="customer_name=" (
            set customer_name=%%b
        )
        if "%%a"=="product_model=" (
            set product_model=%%b
        )
        if "%%a"=="HW_EXT_FLASH_TYPE=" (
            set hw_ext_flash_type=%%b
        )
        if "%%a"=="HW_EXT_FLASH_SIZE=" (
            set hw_ext_flash_size=%%b
        )
        if "%%a"=="HW_EXT_FLASH_NUM=" (
            set hw_ext_flash_num=%%b
        )
    )
)
REM Compute kernel path now that customer_name is known
set folder_dw_kernel_customer=%folder_dw_current%\customers\%customer_name%\kernel
if exist %folder_dw_kernel_customer% (
    set folder_dw_kernel_base=%folder_dw_kernel_customer%
)

if "%para_dw_flash_size%"=="[]" (
    echo %para_dw_flash_size%
    echo load flash size error
    exit /B 1
)
if "%hw_ext_flash_size%"=="" set hw_ext_flash_size=0
if "%hw_ext_flash_num%"=="" set hw_ext_flash_num=1
if "%hw_ext_flash_size%"=="0" set /a hw_ext_flash_size=%para_dw_flash_size%*1024*1024
if "%hw_ext_flash_num%"=="0" set hw_ext_flash_num=1
echo module_version: %module_version%
echo module_flash_szie: %para_dw_flash_size%
echo customer_name: %customer_name%
echo ext_flash_type: %hw_ext_flash_type%
echo ext_flash_size: %hw_ext_flash_size% bytes
echo ext_flash_num:  %hw_ext_flash_num%
set folder_dw_tool=%folder_dw_current%\..\..\Tools\aboot\
set file_dw_download_modify_script=%folder_dw_current%\tools\download_modify.py
set cmd_dw_7z=%folder_dw_current%\prebuilts\win32\bin\7z.exe
set cmd_dw_python=%folder_dw_current%\prebuilts\win32\python3\python3.exe
set glfs_exec=%folder_dw_current%\tools\win32\gen_lfs.exe
set scrip_app_padding_python=%folder_dw_current%\tools\app_padding.py
set app_partition_cfg=%folder_dw_tool%\config\partition\ASR1602_SINGLE_FLASH_LAYOUT_open_04MB.json

set para_dw_target=app
set cmd_dw_del_folder=rmdir /s /q
set cmd_dw_del_file=del /Q
set cmd_dw_file_copy=xcopy /Q /y
set cmd_dw_file_copy_no_prompt=copy /Y
set cmd_dw_folder_copy=RoboCopy /MT[:8] /E

set file_dw_release=[]
set para_dw_release_only=n
set para_dw_keygen=n

REM parse parameters
goto para_dw_parse
:para_dw_parse
if not "%1"=="" (
    if "%1"=="-p" (
        set file_dw_release=%2
        shift /0
    ) else if "%1"=="-t" (
        set folder_dw_tool=%2
        shift /0
    ) else if "%1"=="-b" (
        set folder_dw_bin=%2
        shift /0
    ) else if "%1"=="-app" (
        set para_dw_target=app
    )else if "%1"=="-rf" (
        set para_dw_target=rf
    ) else if "%1"=="-all" (
        set para_dw_target=norf
    ) else if "%1"=="-ALL" (
        set para_dw_target=all
    )else if "%1"=="-wf" (
        set para_dw_target=nofs_norf
    ) else if "%1"=="-r" (
        set para_dw_release_only=y
    ) else if "%1"=="-keygen" (
        set para_dw_keygen=y
    ) else (
        echo unknown para %1
        exit /B 1
    )

    shift /0
    goto para_dw_parse
)


REM check tool
set cmd_dw_release=%folder_dw_tool%\arelease.exe
set cmd_dw_download=%folder_dw_tool%\adownload.exe
if not exist %cmd_dw_release% (
    echo release tool not exist
    exit /B 1
)
if not exist %cmd_dw_download% (
    echo download tool not exist
    exit /B 1
)

if not "%file_dw_release%"=="[]" (
    cd %folder_dw_tool%
    goto download
) 

if "%para_dw_keygen%"=="y" (
    pushd %folder_dw_tool%
    echo Regenerating RSA key pair...
    arelease.exe -c . -k --key-alg=rsa
    if not "%errorlevel%"=="0" (
        echo Key generation failed
        popd
        exit /B 1
    )
    popd
    echo Key generation completed. Skipping packaging and download.
    cd %folder_dw_current%
    exit /B 0
)

REM platform type
set para_dw_platform=[]
set para_dw_variants=[]

if %module_version%==ZX600RG (
    if "%para_dw_flash_size%" == "8" (
        set module_version=%module_version%_SA
        set para_dw_platform=ASR_CRANEL_EVB_08M
        set para_dw_variants=CRANEL_A0_08MB
        
    ) else if "%para_flash_size%" == "4" (
        set module_version=%module_version%_CA
        set para_dw_platform=ASR_CRANEL_EVB_04M
        set para_dw_variants=CRANEL_A0_04MB
    ) else (
        echo "ZX600RG flash size only support 8M and 4M"
        exit /B 1
    )
) else if %module_version%==ZX600CG (
    if "%para_dw_flash_size%"=="16" (
        set para_dw_platform=ASR_CRANE_EVB_16M
        set para_dw_variants=CRANEGM_A0_16MB
    ) else if "%para_dw_flash_size%"=="8" (
        set para_dw_platform=ASR_CRANE_EVB_08M
        set para_dw_variants=CRANEGM_A0_08MB
    ) else (
        echo "ZX600CG flash size only support 16M and 8M"
        exit /B 1
    )
) else if %module_version%==ZX800RG (
    if "%para_dw_flash_size%" == "8" (
        set module_version=%module_version%_SA
        set para_dw_platform=ASR_CRANEL_EVB_08M_TTS
        set para_dw_variants=CRANEL_A0_08MB
        
    ) else if "%para_dw_flash_size%" == "4" (
        set module_version=%module_version%_CA
        set para_dw_platform=ASR_CRANEL_EVB_04M
        set para_dw_variants=CRANEL_A0_04MB
    ) else (
        echo "ZX800RG flash size only support 8M and 4M"
        exit /B 1
    )
) else if %module_version%==ZX800SG (
    if "%para_dw_flash_size%" == "2" (
        set module_version=%module_version%_CA
        set para_dw_platform=ASR1602_EVB
        set para_dw_variants=ASR1602_OPEN_02MB
    ) else if "%para_dw_flash_size%" == "4" (
        set module_version=%module_version%_SA
        set para_dw_platform=ASR1602_EVB
        set para_dw_variants=ASR1602_OPEN_04MB
    ) else (
        echo "ZX800SG flash size only support 4M and 2M"
        exit /B 1
    )
) else if %module_version%==ZX800VG (
    if "%para_dw_flash_size%" == "2" (
        set module_cp2=y
        set module_version=%module_version%_CA
        set para_dw_platform=ASR1605_EVB
        set para_dw_variants=ASR1605_OPEN_02MB
        set app_partition_cfg=%folder_dw_tool%\config\partition\ASR1605_SINGLE_FLASH_LAYOUT_OPEN_02MB_CP2.json
    ) else if "%para_dw_flash_size%" == "4" (
        set module_version=%module_version%_SA
        set para_dw_platform=ASR1605_EVB
        if /I "%hw_ext_flash_type%"=="NOR" (
            set para_dw_variants=ASR1605_OPEN_04MB_NOR
            set app_partition_cfg=%folder_dw_tool%\config\partition\ASR1605_SINGLE_FLASH_LAYOUT_OPEN_04MB_NOR.json
        ) else if /I "%hw_ext_flash_type%"=="NOR_DUL" (
            set para_dw_variants=ASR1605_OPEN_04MB
            set app_partition_cfg=%folder_dw_tool%\config\partition\ASR1605_SINGLE_FLASH_LAYOUT_OPEN_04MB.json
        ) else (
            set para_dw_variants=ASR1605_OPEN_04MB
            set app_partition_cfg=%folder_dw_tool%\config\partition\ASR1605_SINGLE_FLASH_LAYOUT_OPEN_04MB.json
        )
    ) else (
        echo "ZX800VG flash size only support 4M and 2M"
        exit /B 1
    )
    )
    set KERNEL_BASE=%folder_dw_kernel_base%
    set folder_dw_bin=%folder_dw_kernel_base%\%module_version%\

REM load build config
set file_dw_build_config=%folder_dw_bin%\bcfg
set para_dw_release_name=[]
set para_dw_build_git_version=[]
set para_dw_build_time=[]
if exist %file_dw_build_config% (
    for /f "tokens=1,2 delims='" %%a in (%file_dw_build_config%) do (
        if "%%a"=="build_sw_version=" (
            set para_dw_release_name=%%b
        ) 
        if "%%a"=="build_git_version=" (
            set para_dw_build_git_version=%%b
        )
        if "%%a"=="build_time=" (
            set para_dw_build_time=%%b
        )
    )
)

REM Get APP_VERSION_STRING from app_version.h or regenerate if needed
set file_app_version_header=%folder_dw_current%\framework\common\app_version.h
set para_dw_app_version=Unknown
set para_dw_git_dirty=0

REM If app_version.h doesn't exist or customer/product changed, regenerate it
if not exist %file_app_version_header% (
    echo Generating version header...
    if "%product_model%"=="" (
        python %cmd_dw_python% %folder_dw_current%\tools\gen_version.py %customer_name% --sdk-root=%folder_dw_current%
    ) else (
        python %cmd_dw_python% %folder_dw_current%\tools\gen_version.py -c %customer_name% -p %product_model% --sdk-root=%folder_dw_current%
    )
)

if exist %file_app_version_header% (
    for /f "delims=" %%v in ('%cmd_dw_python% %folder_dw_current%\tools\get_version.py %file_app_version_header%') do set para_dw_app_version=%%v
    REM Check if version contains "-dirty" suffix
    echo %para_dw_app_version% | findstr /C:"-dirty" >nul
    if not errorlevel 1 (
        set para_dw_git_dirty=1
    )
)
echo APP_VERSION: %para_dw_app_version%
if "%para_dw_git_dirty%"=="1" (
    echo WARNING: Git repository has uncommitted changes!
)

echo tool: %folder_dw_tool%
echo bin: %folder_dw_bin%
echo target: %para_dw_target%
echo pac: %file_dw_release%

REM check bin
set file_dw_cp_bin=%folder_dw_bin%\cp.bin
set file_dw_cp2_bin=%folder_dw_bin%\cp2.bin
set file_dw_dsp_bin=%folder_dw_bin%\dsp.bin
set file_dw_rf_bin=%folder_dw_bin%\rf.bin
set file_dw_logo_bin=%folder_dw_bin%\logo.bin
set file_dw_updater_bin=%folder_dw_bin%\updater.bin
set file_dw_boot33_bin=%folder_dw_bin%\boot33.bin
set file_dw_app_bin=%folder_dw_bin%\customer_app.bin
set file_dw_capp_tmp_bin=%folder_dw_bin%\capp_tmp.bin
set file_dw_app_pad_bin=%folder_dw_bin%\customer_app_pad.bin
set file_dw_sys_cfg_bin=%folder_dw_bin%\sys_cfg.bin
set file_dw_sys_cfg_csv=%folder_dw_current%\customers\%customer_name%\sys_cfg.csv
set folder_dw_customer_res=%folder_dw_current%\customers\%customer_name%\res
set file_dw_factory_flag=%folder_dw_customer_res%\.factory_mode
set file_dw_factory_flag_legacy=%folder_dw_customer_res%\.factoy_mode
set file_dw_nor_factory_bin=%folder_dw_bin%\nor_data_%customer_name%_factory.bin
set factory_fs_ready=n
set factory_flag_created=0
set factory_lfs_size=

@REM xcopy /Y "\\10.1.200.82\Project\pp\asr160x_sdk_144\SDK\DM_LTEGSM_SDK\startup\bootloader\release\boot33_craneLRH_multiRF.bin" %file_dw_boot33_bin%

REM Ensure factory flag files are removed before normal LFS build
if exist %file_dw_factory_flag% %cmd_dw_del_file% %file_dw_factory_flag%
if exist %file_dw_factory_flag_legacy% %cmd_dw_del_file% %file_dw_factory_flag_legacy%

if "%customer_name%"=="default" (
    set file_dw_nor_bin=%folder_dw_current%\res\nor_data_0910_bank.bin
) else (
    set file_dw_nor_bin=%folder_dw_bin%\nor_data_%customer_name%.bin
    if "%para_dw_flash_size%" == "4" (
        set factory_lfs_size=8*1024*1024
    ) else if "%para_dw_flash_size%" == "2" (
        set factory_lfs_size=6*1024*1024
    )
    if not "!factory_lfs_size!"=="" (
        %glfs_exec% %folder_dw_customer_res% %folder_dw_bin%\nor_data_%customer_name%.bin !factory_lfs_size!
        if exist %file_dw_nor_factory_bin% %cmd_dw_del_file% %file_dw_nor_factory_bin%
        if not exist %file_dw_factory_flag% (
            echo 1>%file_dw_factory_flag%
            set factory_flag_created=1
        )
        %glfs_exec% %folder_dw_customer_res% %file_dw_nor_factory_bin% !factory_lfs_size!
        if exist %file_dw_nor_factory_bin% (
            set factory_fs_ready=y
        ) else (
            if "%factory_flag_created%"=="1" %cmd_dw_del_file% %file_dw_factory_flag%
            echo Factory LFS image generation failed
            goto fail_exit
        )
        if "%factory_flag_created%"=="1" %cmd_dw_del_file% %file_dw_factory_flag%
    ) else (
        echo Skip LFS generation: unsupported flash size %para_dw_flash_size%
    )
)

REM regenerate sys_cfg.bin from CSV so -ALL picks latest IO/flash config
if exist %file_dw_sys_cfg_csv% (
    %cmd_dw_python% %folder_dw_current%\tools\gen_sys_cfg.py --csv %file_dw_sys_cfg_csv% --out %file_dw_sys_cfg_bin% --ext-flash-size %hw_ext_flash_size% --ext-flash-num %hw_ext_flash_num% --print-type
) else (
    %cmd_dw_python% %folder_dw_current%\tools\gen_sys_cfg.py --out %file_dw_sys_cfg_bin% --ext-flash-type %hw_ext_flash_type% --ext-flash-size %hw_ext_flash_size% --ext-flash-num %hw_ext_flash_num% --print-type
)
if not "%errorlevel%"=="0" (
    echo sys_cfg generation failed
    exit /B 1
)

if not exist %file_dw_sys_cfg_bin% (
    echo sys_cfg.bin not exist
    exit /B 1
)

if not exist %file_dw_cp_bin% (
    echo cp.bin not exist
    exit /B 1
)
if not exist %file_dw_dsp_bin% (
    echo dsp.bin not exist
    exit /B 1
)
if not exist %file_dw_rf_bin% (
    echo rf.bin not exist
    exit /B 1
)
if not exist %file_dw_logo_bin% (
    echo logo.bin not exist
    exit /B 1
)
if not exist %file_dw_updater_bin% (
    echo updater.bin not exist
    exit /B 1
)
if not exist %file_dw_boot33_bin% (
    echo boot33.bin not exist
    exit /B 1
)
if "%module_cp2%"=="y" (
    if not exist %file_dw_cp2_bin% (
        echo cp2.bin not exist
        exit /B 1
    )    
)
if not exist %file_dw_app_bin% (
    echo customer_app.bin not exist, copy default[no app]
    echo F | %cmd_dw_file_copy% %folder_dw_tool%\images\customer_app.bin %file_dw_app_bin%
)

if exist %file_dw_app_pad_bin% (
    %cmd_dw_del_file% %file_dw_app_pad_bin%
)

%cmd_dw_python% %scrip_app_padding_python% %file_dw_app_bin% %app_partition_cfg%  %file_dw_app_pad_bin%
if not exist %file_dw_app_pad_bin% (
    echo customer_app_pad.bin not exist, copy default[no app]
    exit /B 1
)
if not exist %file_dw_nor_bin% (
    echo text.bin not exist
    exit /B 1
)
REM goto aboot tool folder
cd %folder_dw_tool%


REM release pac all
set para_dw_release_all_name=%para_dw_app_version%_all[%para_dw_release_name%]
set folder_dw_release_all=.\%para_dw_release_all_name%\
set file_dw_release_all=.\%para_dw_release_all_name%.zip

if "%module_cp2%"=="y" (
    echo arelease.exe -c . -q -g -p %para_dw_platform% -v %para_dw_variants% -i cp=%file_dw_cp_bin%,cp2=%file_dw_cp2_bin%,dsp=%file_dw_dsp_bin%,rfbin=%file_dw_rf_bin%,boot33_bin=%file_dw_boot33_bin%,logo=%file_dw_logo_bin%,updater_bin=%file_dw_updater_bin%,customer_app=%file_dw_app_pad_bin%,sys_cfg=%file_dw_sys_cfg_bin%,text=%file_dw_nor_bin% %file_dw_release_all%
    arelease.exe -c . -q -g -p %para_dw_platform% -v %para_dw_variants% -i cp=%file_dw_cp_bin%,cp2=%file_dw_cp2_bin%,dsp=%file_dw_dsp_bin%,rfbin=%file_dw_rf_bin%,boot33_bin=%file_dw_boot33_bin%,logo=%file_dw_logo_bin%,updater_bin=%file_dw_updater_bin%,customer_app=%file_dw_app_pad_bin%,sys_cfg=%file_dw_sys_cfg_bin%,cust_nvm=%file_dw_nor_bin% %file_dw_release_all%
) else (
    echo arelease.exe -c . -q -g -p %para_dw_platform% -v %para_dw_variants% -i cp=%file_dw_cp_bin%,dsp=%file_dw_dsp_bin%,rfbin=%file_dw_rf_bin%,boot33_bin=%file_dw_boot33_bin%,logo=%file_dw_logo_bin%,updater_bin=%file_dw_updater_bin%,customer_app=%file_dw_app_pad_bin%,sys_cfg=%file_dw_sys_cfg_bin%,text=%file_dw_nor_bin% %file_dw_release_all%
    arelease.exe -c . -q -g -p %para_dw_platform% -v %para_dw_variants% -i cp=%file_dw_cp_bin%,dsp=%file_dw_dsp_bin%,rfbin=%file_dw_rf_bin%,boot33_bin=%file_dw_boot33_bin%,logo=%file_dw_logo_bin%,updater_bin=%file_dw_updater_bin%,customer_app=%file_dw_app_pad_bin%,sys_cfg=%file_dw_sys_cfg_bin%,cust_nvm=%file_dw_nor_bin% %file_dw_release_all%
)
if not "%errorlevel%"=="0" (
    echo Release package error
    goto fail_exit
)

if exist %folder_dw_release_all% (
    %cmd_dw_del_folder% %folder_dw_release_all%
)
md %folder_dw_release_all%
%cmd_dw_7z% x -o%folder_dw_release_all% %file_dw_release_all%

REM set para_dw_build_time=%para_dw_build_time::=%
REM set para_dw_build_time=%para_dw_build_time:-=%
REM set para_dw_build_time=%para_dw_build_time:,=%
REM set para_dw_build_time=%para_dw_build_time: =0%
REM set para_dw_release_dst=%para_dw_release_name%_%para_dw_build_git_version%_%para_dw_build_time%
set para_dw_release_dst=%para_dw_app_version%_map[%para_dw_release_name%]
set folder_dw_release_dst=.\%para_dw_release_dst%\
set file_dw_release_dst=.\%para_dw_release_dst%.zip
if "%para_dw_release_only%" == "y" (
    REM Check if Git is dirty and prompt for confirmation
    if "%para_dw_git_dirty%"=="1" (
        echo.
        echo ============================================================
        echo WARNING: Git repository has UNCOMMITTED CHANGES!
        echo ============================================================
        echo The repository contains uncommitted changes.
        echo Creating a release package from dirty code is NOT recommended.
        echo.
        echo Do you want to continue? (Y/N^)
        set /p user_confirm="> "
        if /i not "!user_confirm!"=="Y" (
            echo Release cancelled by user.
            cd %folder_dw_current%
            exit /B 0
        )
        echo Continuing with dirty repository...
        echo.
    )
    
    echo para_dw_release_dst: %para_dw_release_dst%
    echo folder_dw_release_dst: %folder_dw_release_dst%
    echo file_dw_release_dst: %file_dw_release_dst%

    if exist %file_dw_release_dst% (
        %cmd_dw_del_file% %file_dw_release_dst%
    )
    if exist %folder_dw_release_dst% (
        %cmd_dw_del_folder% %folder_dw_release_dst%
    )
    md %folder_dw_release_dst%

    %cmd_dw_file_copy_no_prompt% %folder_dw_bin%\bcfg %folder_dw_release_dst%\%para_dw_release_name%_bcfg >nul
    %cmd_dw_file_copy_no_prompt% %folder_dw_bin%\cp.axf %folder_dw_release_dst%\%para_dw_release_name%_cp.axf >nul
    %cmd_dw_file_copy_no_prompt% %folder_dw_bin%\cp.map %folder_dw_release_dst%\%para_dw_release_name%_cp.map >nul
    %cmd_dw_file_copy_no_prompt% %folder_dw_bin%\cp_MDB.txt %folder_dw_release_dst%\%para_dw_release_name%_cp_MDB.txt >nul
    %cmd_dw_file_copy_no_prompt% %file_dw_sys_cfg_bin% %folder_dw_release_dst%\%para_dw_release_name%_sys_cfg.bin >nul

    REM Copy bin, elf, map files from out\appimage_debug\hex\
    set folder_dw_app_hex=%folder_dw_current%\out\appimage_debug\hex\
    if exist !folder_dw_app_hex! (
        echo Copying application debug files...
        for %%f in (!folder_dw_app_hex!*.bin) do (
            echo F | %cmd_dw_file_copy% "%%f" %folder_dw_release_dst%
        )
        for %%f in (!folder_dw_app_hex!*.elf) do (
            echo F | %cmd_dw_file_copy% "%%f" %folder_dw_release_dst%
        )
        for %%f in (!folder_dw_app_hex!*.map) do (
            echo F | %cmd_dw_file_copy% "%%f" %folder_dw_release_dst%
        )
    )

    REM call :release_new %para_dw_release_name% norf n
    REM call :release_new %para_dw_release_name% nofs_norf n
    REM call :release_new %para_dw_release_name% rf n
    REM call :release_new %para_dw_release_name% app n
    REM %cmd_dw_file_copy% .\%para_dw_release_name%_norf.zip %folder_dw_release_dst%
    REM %cmd_dw_file_copy% .\%para_dw_release_name%_nofs_norf.zip %folder_dw_release_dst%
    REM %cmd_dw_file_copy% .\%para_dw_release_name%_rf.zip %folder_dw_release_dst%
    REM %cmd_dw_file_copy% .\%para_dw_release_name%_app.zip %folder_dw_release_dst%
    REM %cmd_dw_file_copy% .\%para_dw_release_name%_all.zip %folder_dw_release_dst%

    %cmd_dw_7z% a -tzip %file_dw_release_dst% %folder_dw_release_dst%\* -r

    REM Build factory package in release-only flow before cleanup
    call :release_factory %para_dw_app_version% %para_dw_release_name%

    %cmd_dw_del_folder% %folder_dw_release_dst%
    %cmd_dw_del_folder% %folder_dw_release_all%

    goto success_exit
)

REM generate release target
set file_dw_release=.\%para_dw_app_version%_%para_dw_target%[%para_dw_release_name%].zip
if not "%para_dw_target%"=="all" (
    call :release_new %para_dw_app_version% %para_dw_release_name% %para_dw_target% n
)

%cmd_dw_del_folder% %folder_dw_release_all%

goto download
:download
    REM check download pac
    if not exist %file_dw_release% (
        echo %file_dw_release% not exist
        goto fail_exit
    )

    REM download
    echo start burn %file_dw_release% ......
    adownload.exe -q -r -f -u -a -s 3686400 %file_dw_release%
    if not "%errorlevel%"=="0" (
        echo Download package error
        goto fail_exit
    )

    goto success_exit

:success_exit
    cd %folder_dw_current%
    exit /B 0

:fail_exit
    cd %folder_dw_current%
    exit /B 1 

:release_new
    set para_dw_tmp_app_version=%1
    set para_dw_tmp_release_name=%2
    set para_dw_tmp_target=%3
    set para_dw_tmp_keep_dst_folder=%4
    set folder_dw_tmp_release_all=.\%para_dw_tmp_app_version%_all[%para_dw_tmp_release_name%]\
    set para_dw_tmp_release=%para_dw_tmp_app_version%_%para_dw_tmp_target%[%para_dw_tmp_release_name%]
    set folder_dw_tmp_release=.\%para_dw_tmp_release%\
    set file_dw_tmp_release=.\%para_dw_tmp_release%.zip

    echo ************************************************
    echo release: %para_dw_tmp_release%
    echo ************************************************

    if not exist %folder_dw_tmp_release_all% (
        echo %folder_dw_tmp_release_all% not exist
        exit /B 1
    )

    if exist %file_dw_tmp_release% (
        %cmd_dw_del_file% %file_dw_tmp_release%
    )
    if exist %folder_dw_tmp_release% (
        %cmd_dw_del_folder% %folder_dw_tmp_release%
    )
    md %folder_dw_tmp_release%

    if "%para_dw_tmp_target%"=="norf" (
        %cmd_dw_folder_copy%  %folder_dw_tmp_release_all%  %folder_dw_tmp_release%
        %cmd_dw_del_file% %folder_dw_tmp_release%\rf.bin
        %cmd_dw_del_file% %folder_dw_tmp_release%\download.json
        %cmd_dw_python% %file_dw_download_modify_script% %folder_dw_tmp_release_all%\download.json %folder_dw_tmp_release%\download.json remove flash:rf
    ) else if "%para_dw_tmp_target%"=="nofs_norf" (
        %cmd_dw_folder_copy%  %folder_dw_tmp_release_all%  %folder_dw_tmp_release%
        %cmd_dw_del_file% %folder_dw_tmp_release%\rf.bin
        %cmd_dw_del_file% %folder_dw_tmp_release%\download.json 
        %cmd_dw_python% %file_dw_download_modify_script% %folder_dw_tmp_release_all%\download.json %folder_dw_tmp_release%\download.json remove erase:fs flash:rf
    ) else if "%para_dw_tmp_target%"=="rf" (
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\preboot.img %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\partition.bin %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\flasher.img %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\flashinfo.bin %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\rf.bin %folder_dw_tmp_release%
        %cmd_dw_python% %file_dw_download_modify_script% %folder_dw_tmp_release_all%\download.json %folder_dw_tmp_release%\download.json keep flash:rf
    ) else ( 
        REM if "%para_dw_tmp_target%"=="app" ()
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\preboot.img %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\partition.bin %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\flasher.img %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\flashinfo.bin %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\customer_app.bin %folder_dw_tmp_release%
        %cmd_dw_python% %file_dw_download_modify_script% %folder_dw_tmp_release_all%\download.json %folder_dw_tmp_release%\download.json keep flash:app
    )

    %cmd_dw_7z% a -tzip %file_dw_tmp_release% %folder_dw_tmp_release%\* -r

    if "%para_dw_tmp_keep_dst_folder%"=="n" (
        %cmd_dw_del_folder% %folder_dw_tmp_release%
    )

    goto :EOF

:release_factory
    set para_dw_fact_app_version=%1
    set para_dw_fact_release_name=%2

    set folder_dw_fact_all=.\%para_dw_fact_app_version%_all[%para_dw_fact_release_name%]
    set folder_dw_fact_dst=.\%para_dw_fact_app_version%_factory[%para_dw_fact_release_name%]
    set file_dw_fact_zip=.\%para_dw_fact_app_version%_factory[%para_dw_fact_release_name%].zip

    set factory_skip_reason=
    if "%para_dw_release_only%"=="n" set factory_skip_reason=only generated during release-only (-r)
    if "%customer_name%"=="default" set factory_skip_reason=default customer uses prebuilt NOR image
    if not "%factory_fs_ready%"=="y" set factory_skip_reason=factory LFS not prepared
    if "!factory_lfs_size!"=="" set factory_skip_reason=unsupported flash size %para_dw_flash_size%
    if not exist %file_dw_nor_factory_bin% set factory_skip_reason=factory LFS image missing
    if not exist %folder_dw_fact_all% set factory_skip_reason=base release folder missing

    if not "%factory_skip_reason%"=="" (
        echo Factory package skipped: %factory_skip_reason%
        goto :EOF
    )

    if exist %file_dw_fact_zip% (
        %cmd_dw_del_file% %file_dw_fact_zip%
    )
    if exist %folder_dw_fact_dst% (
        %cmd_dw_del_folder% %folder_dw_fact_dst%
    )

    %cmd_dw_folder_copy%  %folder_dw_fact_all%  %folder_dw_fact_dst%
    %cmd_dw_del_file% %folder_dw_fact_dst%\cust_nvm.bin
    %cmd_dw_file_copy_no_prompt% %file_dw_nor_factory_bin% %folder_dw_fact_dst%\cust_nvm.bin
    if exist %file_dw_fact_zip% %cmd_dw_del_file% %file_dw_fact_zip%
    %cmd_dw_7z% a -tzip %file_dw_fact_zip% %folder_dw_fact_dst%\* -r

    %cmd_dw_del_folder% %folder_dw_fact_dst%

    goto :EOF
