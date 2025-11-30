#!/bin/bash

SOURCE_DIR="$rlora_root/simulations/results/"
count=0

echo "=== Starting export from $SOURCE_DIR ==="

for file in $SOURCE_DIR*.vec; do
    [ -f "$file" ] || continue

    ((count++))
    if [[ "$file" =~ mac([A-Za-z0-9]+)-maxX([0-9]+)m-ttnm([0-9.]+)s-numberNodes([0-9]+)-m([A-Za-z]+)-rep([0-9]+)\.vec ]]; then
        protocol="${BASH_REMATCH[1]}"
        range="${BASH_REMATCH[2]}m"
        interval="${BASH_REMATCH[3]}s"
        nodes="${BASH_REMATCH[4]}"
        mobility="${BASH_REMATCH[5]}"
        rep="${BASH_REMATCH[6]}"
        
        opp_scavetool export -f 'name=~"timeOnAir:vector"' -o $rlora_root/data/${protocol}/${range}/timeOnAir-${mobility}-${count}.json -F JSON ${SOURCE_DIR}mac${protocol}-maxX${range}-ttnm${interval}-numberNodes${nodes}-m${mobility}-rep${rep}.vec
              
        opp_scavetool export -f 'name=~"missionIdRtsSent:vector" OR name=~"missionIdFragmentSent:vector" OR name=~"receivedMissionId:vector"' -o $rlora_root/data/${protocol}/${range}/missionId-${mobility}-${count}.json -F JSON ${SOURCE_DIR}mac${protocol}-maxX${range}-ttnm${interval}-numberNodes${nodes}-m${mobility}-rep${rep}.vec

        opp_scavetool export -f 'name=~"timeOfLastTrajectory:vector"' -o $rlora_root/data/${protocol}/${range}/timeOfLastTrajectory-${mobility}-${count}.json -F JSON ${SOURCE_DIR}mac${protocol}-maxX${range}-ttnm${interval}-numberNodes${nodes}-m${mobility}-rep${rep}.vec

        opp_scavetool export -f 'name=~"receivedId:vector"' -o $rlora_root/data/${protocol}/${range}/idReceived-${mobility}-${count}.json -F JSON ${SOURCE_DIR}mac${protocol}-maxX${range}-ttnm${interval}-numberNodes${nodes}-m${mobility}-rep${rep}.vec

    fi
done

for file in ${SOURCE_DIR}*.txt; do
    [ -f "$file" ] || continue

    ((count++))
    if [[ "$file" =~ mac([A-Za-z0-9]+)-maxX([0-9]+)m-ttnm([0-9.]+)s-numberNodes([0-9]+)-m([A-Za-z]+)-rep([0-9]+)\.txt ]]; then
        protocol="${BASH_REMATCH[1]}"
        range="${BASH_REMATCH[2]}m"
        
        DEST_DIR="$rlora_root/data/${protocol}/${range}/"
        cp "$file" "$DEST_DIR"
    fi
done

