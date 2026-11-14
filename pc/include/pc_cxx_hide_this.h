/* Force-included before C files compiled as C++ with -Dthis=this_arg.
 * libstdc++ uses the keyword this. Parse those headers while the macro is off
 * so their include guards stick and a later #include <math.h> does not re-enter them. */
#ifdef this
#pragma push_macro("this")
#undef this
#define PC_CXX_HIDE_THIS_POP 1
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#ifdef PC_CXX_HIDE_THIS_POP
#pragma pop_macro("this")
#undef PC_CXX_HIDE_THIS_POP
#endif
