// Force-included into RecompiledFuncs only.
#ifndef CONKER_HOST_PROTOS_H
#define CONKER_HOST_PROTOS_H

#include <setjmp.h>
#include "recomp.h"

#undef RECOMP_FUNC
#define RECOMP_FUNC __attribute__((weak, noipa, optimize("rounding-math")))

#endif
