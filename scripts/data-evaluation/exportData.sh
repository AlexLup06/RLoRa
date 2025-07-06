#!/bin/bash


SOURCE_DIR="$rlora_root/simulations/results/"
count=0
for file in $SOURCE_DIR*.vec; do
    [ -f "$file" ] || continue

    ((count++))
    if [[ "$file" =~ mac([A-Za-z]+)-maxX([0-9]+)m-ttnm([0-9.]+)s-numberNodes([0-9]+)-m([A-Za-z]+)\.vec ]]; then
        protocol="${BASH_REMATCH[1]}"
        range="${BASH_REMATCH[2]}m"
        interval="${BASH_REMATCH[3]}s"
        nodes="${BASH_REMATCH[4]}"
        mobility="${BASH_REMATCH[5]}"


        opp_scavetool export -f 'name=~"timeOnAir:vector"' -o $rlora_root/data/${protocol}/${range}/timeOnAir-${count}.json -F JSON ${SOURCE_DIR}mac${protocol}-maxX${range}-ttnm${interval}-numberNodes${nodes}-m${mobility}.vec
 
        opp_scavetool export -f 'name=~"effectiveThroughputBps:vector"' -o $rlora_root/data/${protocol}/${range}/effectiveThroughput-${count}.json -F JSON ${SOURCE_DIR}mac${protocol}-maxX${range}-ttnm${interval}-numberNodes${nodes}-m${mobility}.vec

        opp_scavetool export -f 'name=~"throughputBps:vector"' -o $rlora_root/data/${protocol}/${range}/throughput-${count}.json -F JSON ${SOURCE_DIR}mac${protocol}-maxX${range}-ttnm${interval}-numberNodes${nodes}-m${mobility}.vec
 
	opp_scavetool export -f 'name=~"timeInQueue:vector"' -o $rlora_root/data/${protocol}/${range}/timeInQueue-${count}.json -F JSON ${SOURCE_DIR}mac${protocol}-maxX${range}-ttnm${interval}-numberNodes${nodes}-m${mobility}.vec
    fi
done

for file in ${SOURCE_DIR}*.txt; do
    [ -f "$file" ] || continue

    ((count++))
    if [[ "$file" =~ mac([A-Za-z]+)-maxX([0-9]+)m-ttnm([0-9.]+)s-numberNodes([0-9]+)-m([A-Za-z]+)\.txt ]]; then
        protocol="${BASH_REMATCH[1]}"
        range="${BASH_REMATCH[2]}m"
        interval="${BASH_REMATCH[3]}s"
        nodes="${BASH_REMATCH[4]}"
        mobility="${BASH_REMATCH[5]}"
        
        DEST_DIR="$rlora_root/data/${protocol}/${range}/"
        mv "$file" "$DEST_DIR"
    fi
done


# iterare through all filed and for each simulation run export the data
# sort the data as follows:
# 	- separate folder for each mobility model and then inside there separate folder for each network size and then a foler for the mac protocol
# 	- in each folder put the result in JSON format for the ttnt and numberOfNodes
