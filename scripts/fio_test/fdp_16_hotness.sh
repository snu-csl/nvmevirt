#!/bin/bash

echo "Number of Reclaim Unit Handles you want to use? (e.g. 8)"
read NUM_RUH
echo "$NUM_RUH RUHs for 16 hotness workload"
echo "Random Write for how long? (e.g. 1m)"
read TIME

DIRNAME=~/aos/nvmevirt/scripts/fio_test
cd $DIRNAME

# number of RUH
# NUM_RUH=$1
DIVIDE=$((16 / NUM_RUH))

for i in $(seq 0 15)
do
    eval "RUH$i=$((i / DIVIDE))"
    eval "echo RUH for area $i \(\$i~\$((i + 1))G\) = \$RUH$i"
done

sleep 1
echo Full Sequential Write in 16 Areas
sudo RUH0=$RUH0 RUH1=$RUH1 RUH2=$RUH2 RUH3=$RUH3 RUH4=$RUH4 RUH5=$RUH5 RUH6=$RUH6 RUH7=$RUH7 RUH8=$RUH8 RUH9=$RUH9 RUH10=$RUH10 RUH11=$RUH11 RUH12=$RUH12 RUH13=$RUH13 RUH14=$RUH14 RUH15=$RUH15 fio fdp_16_precnd.fio

sleep 1
echo Random Write for ${TIME}
sudo TIME=$TIME RUH0=$RUH0 RUH1=$RUH1 RUH2=$RUH2 RUH3=$RUH3 RUH4=$RUH4 RUH5=$RUH5 RUH6=$RUH6 RUH7=$RUH7 RUH8=$RUH8 RUH9=$RUH9 RUH10=$RUH10 RUH11=$RUH11 RUH12=$RUH12 RUH13=$RUH13 RUH14=$RUH14 RUH15=$RUH15 fio fdp_16_hotness.fio
