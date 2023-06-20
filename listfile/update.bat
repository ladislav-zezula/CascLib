@echo off
echo  - Listfile for WoW 8.2 or newer ----------------------------------------------------
ren   listfile.csv listfile-old.csv
wget  https://github.com/wowdev/wow-listfile/raw/master/community-listfile.csv -O listfile-new.csv
start /wait CascView.exe /merge "listfile-old.csv+listfile-new.csv" listfile.csv
del   listfile-old.csv
del   listfile-new.csv

echo  - Listfile for WoW pre 8.2 ---------------------------------------------------------
ren   listfile.txt listfile-old.txt
wget  https://github.com/wowdev/wow-listfile/raw/master/listfile.txt -O listfile-new.txt
start /wait CascView.exe /merge "listfile-old.txt+listfile-new.txt" listfile.txt
del   listfile-old.txt
del   listfile-new.txt
