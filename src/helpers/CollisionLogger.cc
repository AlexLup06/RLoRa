#include "CollisionLogger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace rlora {

// Initialisierung der statischen Instanz
CollisionLogger *CollisionLogger::instance = nullptr;

// Singleton-Zugriff
CollisionLogger* CollisionLogger::getInstance()
{
    if (!instance) {
        instance = new CollisionLogger();
    }
    return instance;
}

// Kollision registrieren
void CollisionLogger::logCollision(int id1, int id2)
{
    // Immer in kanonischer Form speichern: kleinerer zuerst
    int minId = min(id1, id2);
    int maxId = max(id1, id2);

    ostringstream oss;
    oss << minId << "," << maxId;

    collisionSet.insert(oss.str());
}

void CollisionLogger::logPossibleCollision(int id1, int id2)
{
    int minId = min(id1, id2);
    int maxId = max(id1, id2);

    ostringstream oss;
    oss << minId << "," << maxId;

    possibleCollisionSet.insert(oss.str());
}

// In Datei schreiben
void CollisionLogger::writeToFile(const string &filename)
{
    ofstream out(filename);
    if (!out.is_open()) {
        throw runtime_error("Fehler beim Öffnen der Datei " + filename);
    }

    out << collisionSet.size() << "\n" << possibleCollisionSet.size() << endl;

    out.close();
}
}
