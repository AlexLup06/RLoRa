#ifndef HELPERS_DATALOGGER_H_
#define HELPERS_DATALOGGER_H_

#include <string>
#include <set>

namespace rlora
{

    using namespace std;

    class DataLogger
    {
    private:
        static DataLogger *instance;

        // Kollisionen als Strings im Format "id1,id2"
        set<string> collisionSet;
        set<string> possibleCollisionSet;

        int transmissions = 0;
        int effectiveTransmissions = 0;

        int receptions = 0;
        int effectiveReceptions = 0;

        int effectiveBytesSent = 0;
        int bytesSent = 0;

        int effectiveBytesReceived = 0;
        int bytesReceived = 0;

        int effectiveBytesReceivedIncludingCollisions = 0;
        int bytesReceivedIncludingCollisions = 0;

        DataLogger() {}
        virtual ~DataLogger();

    public:
        DataLogger(const DataLogger &) = delete;
        DataLogger &operator=(const DataLogger &) = delete;

        static DataLogger *getInstance();

        void clear();

        void logEffectiveTransmission();                             // ✅
        void logTransmission();                                      // ✅

        void logCollision(int id1, int id2);                         // ✅
        void logPossibleCollision(int id1, int id2);                 // ✅

        void logEffectiveReceptions();                               // ✅
        void logReceptions();                                        // ✅

        void logEffectiveBytesSent(int size);                        // ✅
        void logBytesSent(int size);                                 // ✅

        void logEffectiveBytesReceived(int size);                    // ✅
        void logBytesReceived(int size);                             // ✅

        void logEffectiveBytesReceivedIncludingCollisions(int size); // ✅
        void logBytesReceivedIncludingCollisions(int size);          // ✅

        // Am Ende schreiben
        void writeDataToFile(const std::string &filename = "data.txt");
    };

}

#endif
