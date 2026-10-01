/* daisy.h -- what TEMPO's sources expect from libDaisy, on Linux.
 * See ../../daisy_compat.h. The firmware's headers lean on namespaces opened
 * by whichever header came first; this opens them once, here. */
#pragma once
#include "../../daisy_compat.h"
#include <algorithm>
#include <string>
#include <vector>
#include <stdlib.h>
namespace chompi
{
}
using namespace daisy;
using namespace daisysp;
