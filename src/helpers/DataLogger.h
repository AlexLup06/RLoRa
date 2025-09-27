/*
 * DataLogger.h
 *
 *  Created on: 29 Jun 2025
 *      Author: alexanderlupatsiy
 */

#ifndef HELPERS_DATALOGGER_H_
#define HELPERS_DATALOGGER_H_

#include <string>
#include <set>

namespace rlora {

using namespace std;

class DataLogger
{
private:
    static DataLogger *instance;

    // Kollisionen als Strings im Format "id1,id2"
    set<string> collisionSet;
    set<string> possibleCollisionSet;

    int effectiveBytesReceived = 0;
    int effectiveBytesSent = 0;

    int bytesReceived = 0;
    int bytesSent = 0;

    DataLogger()
    {
    }

public:
    DataLogger(const DataLogger&) = delete;
    DataLogger& operator=(const DataLogger&) = delete;

    static DataLogger* getInstance();

    void logCollision(int id1, int id2);
    void logPossibleCollision(int id1, int id2);
    void logEffectiveBytesReceived(int size);
    void logEffectiveBytesSent(int size);
    void logBytesReceived(int size);
    void logBytesSent(int size);

    // Am Ende schreiben
    void writeDataToFile(const std::string &filename = "data.txt");
};

} /* namespace rlora */

#endif /* HELPERS_DATALOGGER_H_ */
