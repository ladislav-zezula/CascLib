wget https://github.com/wowdev/wow-listfile/raw/master/listfile.txt -O listfile6x-new.txt
type listfile6x-new.txt | sort > listfile6x-sorted.txt
del listfile6x-new.txt

wget https://wow.tools/casc/listfile/download/csv -O listfile8x-new.csv
type listfile8x-new.csv | sort > listfile8x-sorted.csv
del listfile8x-new.csv
