#ifndef SUBTITLER_CLI_H
#define SUBTITLER_CLI_H
// Headless entry points used by the reproducible test harness.
#include <QString>
#include "ProjectStore.h"

namespace sub {
int runCli(int argc, char **argv);
}
#endif
