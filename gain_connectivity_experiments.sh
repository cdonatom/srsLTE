#! /bin/bash

if [ "$#" -ne 2 ];then
    echo "Execute as: $0 <rep_experiments> <num_experiments>"
    exit 1
fi
REPS=$1
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

CONN_RESULTS=gain_conn_results/exp$2

GUARD_TIME=10

SSH="ssh -t -o LogLevel=QUIET"

let "WAIT_UE=$UE_TIME+$GUARD_TIME"

function kill_processes {
    $SSH $ENB_ADDR "sudo pkill screen"
    $SSH $UE_ADDR "sudo pkill screen"
}

echo "$(date) | Creating folder for experimentes at $CONN_RESULTS"
$SSH $UE_ADDR "mkdir -p $CONN_RESULTS"

echo "$(date) | Killing previous screens"
kill_processes  

# Fixing TX and RX for eNB

echo "$(date) | eNB TX = 55, RX = 50"
$SSH $ENB_ADDR "sed -i \"s/tx_gain = .*/tx_gain = 55/g\" $ENB_PATH/$ENB_CONF"
$SSH $ENB_ADDR "sed -i \"s/rx_gain = .*/rx_gain = 50/g\" $ENB_PATH/$ENB_CONF"

for ((gain=35;gain<=75;gain+=2)){
    echo "$(date) | UE: TX = $gain db"
    $SSH $UE_ADDR "sed -i \"s/tx_gain = .*/tx_gain = $gain/g\" $UE_PATH/$UE_CONF"

    for ((rep=1;rep<=$REPS;rep++)){

        # Loop for 1.4, 5, 10, 15 and 20 MHz
        for bw in 6 25 50 75 100
        do
            echo "$(date) | Setting n_prb = $bw"
            $SSH $ENB_ADDR "sed -i \"s/n_prb = .*/n_prb = $bw/g\" $ENB_PATH/$ENB_CONF"
            
            echo "$(date) | Setting CPU scheduler as \"performance\""
            $SSH $ENB_ADDR "for((core=0; core < $(grep -c ^processor /proc/cpuinfo);core++));do sudo cpufreq-set -c \$core -g performance; done"
            $SSH $UE_ADDR "for((core=0; core < $(grep -c ^processor /proc/cpuinfo);core++));do sudo cpufreq-set -c \$core -g performance; done"

            echo "$(date) | Launching srsLTE eNB"
            $SSH $ENB_ADDR "screen -dmS $ENB_SCREEN sh; screen -S $ENB_SCREEN -X stuff \"cd $ENB_PATH; sudo ./$ENB_EXEC --enb.name=$ENB_NAME $ENB_CONF\n\""
            sleep $GUARD_TIME

            echo "$(date) | Launching srsLTE UE"
            $SSH $UE_ADDR "screen -dmS $UE_SCREEN sh; screen -S $UE_SCREEN -X stuff \"cd $UE_PATH; sudo ./$UE_EXEC $UE_CONF\n\""
            sleep $WAIT_UE
            if $($SSH $UE_ADDR "sudo ip r a $ENB_ADDR dev tun_srsue" | grep -q "Cannot" ); then
                echo "$(date) | UE: Connection failure"
                $SSH $UE_ADDR "echo \"$bw $gain 0\" >>$CONN_RESULTS/result_$2.txt"

            else
                echo "$(date) | UE: Connection successfully"
                $SSH $UE_ADDR "echo \"$bw $gain 1\" >>$CONN_RESULTS/result_$2.txt"
            fi
            echo "$(date) | Killing processes"
            kill_processes
        done
    }
}
