#!/bin/bash

OUTFILE1=start-1.sh
OUTFILE2=start-2.sh

# Remove if it exists
> "$OUTFILE1"
> "$OUTFILE2"

# Range parameters
STEP=500
END=256000   # we stop at 153600
START=243200

while [ $START -lt $END ]; do
    STOP=$((START + STEP - 1))

    # make sure we don't go beyond END
    if [ $STOP -ge $END ]; then
        STOP=$((END - 1))
    fi

    echo "opp_runall -j80 ./out/clang-release/src/rlora -f ./simulations/omnetpp.ini -c MassMobility -u Cmdenv -n ./simulations:./src:./../inet4.4/src -l ./../inet4.4/src/INET -r ${START}..${STOP}" >> "$OUTFILE1"

    START=$((STOP + 1))
done

chmod +x $OUTFILE1
echo "Generated $OUTFILE1"

START=243200
while [ $START -lt $END ]; do
    STOP=$((START + STEP - 1))

    # make sure we don't go beyond END
    if [ $STOP -ge $END ]; then
        STOP=$((END - 1))
    fi

    echo "opp_runall -j80 ./out/clang-release/src/rlora -f ./simulations/omnetpp.ini -c GaussMarkovMobility -u Cmdenv -n ./simulations:./src:./../inet4.4/src -l ./../inet4.4/src/INET -r ${START}..${STOP}" >> "$OUTFILE2"

    START=$((STOP + 1))
done

chmod +x $OUTFILE2
echo "Generated $OUTFILE2"
