#include "DataLogger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace rlora
{

    DataLogger *DataLogger::instance = nullptr;

    DataLogger::~DataLogger()
    {
        clear();
    }

    void DataLogger::clear()
    {
        collisionSet.clear();
        possibleCollisionSet.clear();

        transmissions = 0;
        effectiveTransmissions = 0;

        receptions = 0;
        effectiveReceptions = 0;

        effectiveBytesSent = 0;
        effectiveBytesReceived = 0;
        effectiveBytesReceivedIncludingCollisions = 0;

        bytesSent = 0;
        bytesReceived = 0;
        bytesReceivedIncludingCollisions = 0;
    }

    DataLogger *DataLogger::getInstance()
    {
        if (!instance)
        {
            instance = new DataLogger();
        }
        return instance;
    }

    void DataLogger::logCollision(int id1, int id2)
    {
        int minId = min(id1, id2);
        int maxId = max(id1, id2);

        ostringstream oss;
        oss << minId << "," << maxId;

        collisionSet.insert(oss.str());
    }

    void DataLogger::logEffectiveBytesReceived(int size)
    {
        effectiveBytesReceived += size;
    }
    void DataLogger::logEffectiveBytesReceivedIncludingCollisions(int size)
    {
        effectiveBytesReceivedIncludingCollisions += size;
    }
    void DataLogger::logBytesReceived(int size)
    {
        bytesReceived += size;
    }
    void DataLogger::logBytesReceivedIncludingCollisions(int size)
    {
        bytesReceivedIncludingCollisions += size;
    }

    void DataLogger::logEffectiveTransmission()
    {
        effectiveTransmissions++;
    }

    void DataLogger::logEffectiveBytesSent(int size)
    {
        effectiveBytesSent += size;
    }

    void DataLogger::logTransmission()
    {
        transmissions++;
    }

    void DataLogger::logReceptions()
    {
        receptions++;
    }

    void DataLogger::logEffectiveReceptions()
    {
        effectiveReceptions++;
    }

    void DataLogger::logBytesSent(int size)
    {
        bytesSent += size;
    }

    void DataLogger::logPossibleCollision(int id1, int id2)
    {
        int minId = min(id1, id2);
        int maxId = max(id1, id2);

        ostringstream oss;
        oss << minId << "," << maxId;

        possibleCollisionSet.insert(oss.str());
    }

    void DataLogger::writeDataToFile(const string &filename)
    {
        ofstream out(filename);
        if (!out.is_open())
        {
            throw runtime_error("Fehler beim Öffnen der Datei " + filename);
        }

        out << "Collisions\n"
            << collisionSet.size() << "\n"
            << possibleCollisionSet.size() << endl;
        out << "Transmissions\n"
            << effectiveTransmissions << "\n"
            << transmissions << endl;
        out << "Receptions\n"
            << effectiveReceptions << "\n"
            << receptions << endl;
        out << "Bytes Received\n"
            << effectiveBytesReceived << "\n"
            << bytesReceived << endl;
        out << "Bytes Received including Collisions\n"
            << effectiveBytesReceivedIncludingCollisions << "\n"
            << bytesReceivedIncludingCollisions << endl;
        out << "Bytes Sent\n"
            << effectiveBytesSent << "\n"
            << bytesSent << endl;

        out.close();
        clear();
    }
}
