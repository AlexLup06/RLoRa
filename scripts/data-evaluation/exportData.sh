#!/bin/bash

mkdir $root/data
opp_scavetool export -f 'module=~Network? AND type=~vector' -o $root/data/$1/client.json -F JSON $root/${3}/flash_s${3}net?cp${2}rid?.vec

# iterare through all filed and for each simulation run export the data
# sort the data as follows:
# 	- separate folder for each mobility model and then inside there separate folder for each network size and then a foler for the mac protocol
# 	- in each folder put the result in JSON format for the ttnt and numberOfNodes
