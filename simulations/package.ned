package rlora.simulations;

import rlora.loraSpecific.LoraNode.LoRaNode;
import rlora.loraSpecific.LoRaPhy.LoRaMedium;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;

@license(LGPL);
network LoRaNetworkTest
{
    parameters:
        int numberOfNodes = default(2);
        int networkSizeX = default(500);
        int networkSizeY = default(500);
        @display("bgb=400,400");
    submodules:
        loRaNodes[numberOfNodes]: LoRaNode {
            @display("p=400,304");
        }
        LoRaMedium: LoRaMedium {
            @display("p=309,102");
        }  
        configurator: Ipv4NetworkConfigurator {
            parameters:
                assignDisjunctSubnetAddresses = false;
                @display("p=318,27");
        }
}

