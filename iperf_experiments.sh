#! /bin/bash

if [ "$#" -ne 1 ];then
    echo "Execute as: $0 <num_experiments>"
    exit 1
fi

UE_ADDR=192.168.70.100
ENB_ADDR=192.168.70.110

ENB_SCREEN=srsenb_session
ENB_NAME=srstest04
ENB_CONF=enb_movistar.conf
ENB_PATH=/home/pablo/Documents/srsLTE_cdonato/build/srsenb/src/
ENB_EXEC=srsenb
UE_PATH=/home/pablo/Documents/srsLTE/build/srsue/src/
UE_SCREEN=srsue_session
UE_CONF=ue.conf
UE_EXEC=srsue
UE_TIME=12

IPERF_RESULTS=iperf_results
IPERF_SCREEN=iperf_session
IPERF_TIME=10
GUARD_TIME=10

let "WAIT_IPERF=$IPERF_TIME+$GUARD_TIME"
let "WAIT_UE=$UE_TIME+$GUARD_TIME"

function kill_processes {
    ssh -t $ENB_ADDR "sudo pkill screen"
    ssh -t $UE_ADDR "sudo pkill screen"
}

echo "Killing previous screens"
kill_processes  

# Loop for 1.4, 5, 10, 15 and 20 MHz
for bw in 6 25 50 75 100
do
    echo "Setting n_prb = $bw"
    ssh -t $ENB_ADDR "sed -i \"s/n_prb = .*/n_prb = $bw/g\" $ENB_PATH/$ENB_CONF"
    echo "Setting gains"
    case $bw in
        6) echo "UE: TX = 55 db; eNB TX = 60, RX = 55"
            ssh -t $ENB_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 60/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $ENB_ADDR "sed -i \"s/rx_gain = .*/rx_gain = 55/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $UE_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 55/g\" $UE_PATH/$UE_CONF"
            ;;
        25) echo "UE: TX = 55 db; eNB TX = 55, RX = 55"
            ssh -t $ENB_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 55/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $ENB_ADDR "sed -i \"s/rx_gain = .*/rx_gain = 55/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $UE_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 55/g\" $UE_PATH/$UE_CONF"
            ;;
        50) echo "UE: TX = 50 db; eNB TX = 55, RX = 50"
            ssh -t $ENB_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 55/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $ENB_ADDR "sed -i \"s/rx_gain = .*/rx_gain = 50/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $UE_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 50/g\" $UE_PATH/$UE_CONF"
            ;;
        75) echo "UE: TX = 50 db; eNB TX = 50, RX = 50"
            ssh -t $ENB_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 50/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $ENB_ADDR "sed -i \"s/rx_gain = .*/rx_gain = 50/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $UE_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 50/g\" $UE_PATH/$UE_CONF"
            ;;
        100) echo "UE: TX = 45 db; eNB TX = 45, RX = 45"
            ssh -t $ENB_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 45/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $ENB_ADDR "sed -i \"s/rx_gain = .*/rx_gain = 45/g\" $ENB_PATH/$ENB_CONF"
            ssh -t $UE_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 45/g\" $UE_PATH/$UE_CONF"
            ;;
    esac

    for rep in {1..$1}
    do
        echo "Launching srsLTE eNB"
        ssh -t $ENB_ADDR "screen -dmS $ENB_SCREEN sh; screen -S $ENB_SCREEN -X stuff \"cd $ENB_PATH; sudo ./$ENB_EXEC --enb.name=$ENB_NAME $ENB_CONF\n\""
        sleep $GUARD_TIME
        echo "Launching iPerf server at eNB"
        ssh -t $ENB_ADDR "screen -dmS $IPERF_SCREEN sh; screen -S $IPERF_SCREEN -X stuff \"iperf3 -s\n\""

        echo "Launching srsLTE UE"
        ssh -t $UE_ADDR "screen -dmS $UE_SCREEN sh; screen -S $UE_SCREEN -X stuff \"cd $UE_PATH; sudo ./$UE_EXEC $UE_CONF\n\""
        sleep $WAIT_UE
        ssh -t $UE_ADDR "sudo ip r a $ENB_ADDR dev tun_srsue"
        sleep $GUARD_TIME

        echo "Launching iPerf client at UE in DL"
        ssh -t $UE_ADDR "screen -dmS $IPERF_SCREEN sh; screen -S $IPERF_SCREEN -X stuff \"mkdir -p $IPERF_RESULTS; iperf3 -c $ENB_ADDR -i 1 -t $IPERF_TIME > $IPERF_RESULTS/iperf_$bw\_prb_ul_$rep.txt\n\""
        sleep $WAIT_IPERF
        echo "Launching iPerf client at UE in UL"
        ssh -t $UE_ADDR "screen -S $IPERF_SCREEN -X stuff \"iperf3 -c $ENB_ADDR -i 1 -t $IPERF_TIME -R >  $IPERF_RESULTS/iperf_$bw\_prb_dl_$rep.txt\n\""
        sleep $WAIT_IPERF

        echo "Killing processes"
        kill_processes
    done
done


