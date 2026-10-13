@echo off
set folder_ab_current=%cd%
set glfs_exec=%folder_ab_current%\tools\win32\gen_lfs.exe
set glfs_dir_src=%folder_ab_current%\res\res_Goly
set glfs_dst=%folder_ab_current%\res\nor_data_Goly.bin


%glfs_exec% %glfs_dir_src% %glfs_dst% 8*1024*1024