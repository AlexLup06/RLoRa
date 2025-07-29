/*
 * CollisionLogger.h
 *
 *  Created on: 29 Jun 2025
 *      Author: alexanderlupatsiy
 */

#ifndef HELPERS_COLLISIONLOGGER_H_
#define HELPERS_COLLISIONLOGGER_H_

#include <string>
#include <set>

namespace rlora {

using namespace std;

class CollisionLogger
{
private:
    static CollisionLogger *instance;

    // Kollisionen als Strings im Format "id1,id2"
    set<string> collisionSet;
    set<string> possibleCollisionSet;

    // Konstruktor privat (Singleton)
    CollisionLogger()
    {
    }

public:
    // Kein Kopieren erlaubt
    CollisionLogger(const CollisionLogger&) = delete;
    CollisionLogger& operator=(const CollisionLogger&) = delete;

    // Zugriff auf Singleton-Instanz
    static CollisionLogger* getInstance();

    // Neue Kollision eintragen
    void logCollision(int id1, int id2);
    void logPossibleCollision(int id1, int id2);

    // Am Ende schreiben
    void writeToFile(const std::string &filename = "collisions.txt");
};

} /* namespace rlora */

#endif /* HELPERS_COLLISIONLOGGER_H_ */
