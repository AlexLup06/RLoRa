package rlora.LoRaMeshRouter;

import inet.linklayer.base.MacProtocolBase;
import inet.linklayer.contract.IMacProtocol;

module LoRaMeshRouter extends MacProtocolBase like IMacProtocol
{
    parameters:
        int headerLength @unit(B) = default(17B); // maximum 255 byte, minimum 17B for serializing, see CsmaCaMacDataHeader
        string radioModule = default("^.radio"); // The path to the Radio module 
        string address @mutable = default("auto");
        double bitrate @unit(bps) = 250bps; // 802.15.4-2006 - IEEE Standard for Information technology
        @class(LoRaMeshRouter);
}
