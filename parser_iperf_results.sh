#! /bin/bash

if [ "$#" -ne 1 ];then
    echo "Execute as: $0 <path_files>"
    exit 1
fi

OFFSET_DL_HEAD=5
OFFSET_DL_TAIL=6
OFFSET_UL_HEAD=4
OFFSET_UL_TAIL=6

RESULTS=$1

echo "Parsing DL files"
for file in $RESULTS/iperf_*dl*
do
    echo "$file"
    tail -n +$OFFSET_DL_HEAD $file | head -n -$OFFSET_DL_TAIL | sed 's/  */ /g' > ${file/.txt/"_parsed.txt"}
done

echo "Parsing UL files"
for file in $RESULTS/iperf_*ul*
do
    echo "$file"
    tail -n +$OFFSET_UL_HEAD $file | head -n -$OFFSET_UL_TAIL | sed 's/  */ /g' > ${file/.txt/"_parsed.txt"}
done
