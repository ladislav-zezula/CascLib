@echo off
echo  - Listfile for WoW 8.2 or newer ----------------------------------------------------
ren   listfile8x.csv listfile8x-old.csv
wget  https://wow.tools/casc/listfile/download/csv -O listfile8x-new.csv
start /wait CascView.exe /merge "listfile8x-old.csv+listfile8x-new.csv" listfile8x.csv
del   listfile8x-old.csv
del   listfile8x-new.csv

echo  - Listfile for WoW pre 8.2 ---------------------------------------------------------
ren   listfile6x.txt listfile6x-old.txt
wget  https://github.com/wowdev/wow-listfile/raw/master/listfile.txt -O listfile6x-new.txt
start /wait CascView.exe /merge "listfile6x-old.txt+listfile6x-new.txt" listfile6x.txt
del   listfile6x-old.txt
del   listfile6x-new.txt
