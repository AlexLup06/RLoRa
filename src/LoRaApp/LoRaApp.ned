package rlora.LoRaApp;

import inet.applications.contract.IApp;

simple LoRaApp like IApp
{
    parameters:
        @class(LoRaApp);
        volatile double timeToFirstTrajectory @unit(s) = default(10s);
        volatile double timeToFirstMission @unit(s) = default(10s);
        volatile double timeToNextTrajectory @unit(s) = default(10s);
        volatile double timeToNextMission @unit(s) = default(10s);
        
        double initialLoRaTP @unit(dBm) = default(14dBm);
        double initialLoRaCF @unit(Hz) = default(868MHz);
        int initialLoRaSF = default(12);
        double initialLoRaBW @unit(Hz) = default(125kHz);
        int initialLoRaCR = default(4);
        bool initialUseHeader = default(true);
    gates:
        input socketIn @labels(LoRaAppPacket/up);
        output socketOut @labels(LoRaAppPacket/down);
}
