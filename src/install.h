// Installing, uninstalling and updating: all done by WaveMini.exe itself.
//  - WaveMini-Setup.exe (the same program under another name, or "--install") copies itself to
//    your programs folder, adds a Start menu shortcut and an entry in Windows' installed apps.
//  - "--uninstall" takes all of that away again.
//  - An update is the new WaveMini.exe from the latest GitHub release, swapped in for this one.
#pragma once
#include "core.h"

namespace install {
// Handles --install / --uninstall (and running as "...Setup.exe"). True: done, exit now.
bool handle(int argc, wchar_t** argv);
// Removes what the last update left behind.
void cleanUp();
// Looks for a newer release and swaps it in. [byUser]: say what happened, and restart into it.
void checkForUpdate(bool byUser);
// Once a day, quietly.
void checkDaily();
}
