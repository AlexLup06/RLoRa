#!/bin/bash

opp_runall -j50 ../out/clang-release/src/rlora -f omnetpp.ini -c MassMobility -u Cmdenv -n .:../src:../../inet4.4/src -l ../../inet4.4/src/INET

opp_runall -j50 ../out/clang-release/src/rlora -f omnetpp.ini -c GausMarkovMobility -u Cmdenv -n .:../src:../../inet4.4/src -l ../../inet4.4/src/INET
