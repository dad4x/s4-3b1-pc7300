/*
 * Get c99 fixed types, somehow
 */

#ifndef S4_FIXED_H
#define S4_FIXED_H

/* normal defs good on 32 and 64 bit machines */

/* if ancient GCC, define it. */
#ifndef __STDC_VERSION__
   #define __STDC_VERSION__   0L
#endif

#if __STDC_VERSION__ >= 199901L
    #include <stdint.h>

    /* we have these automatically */

#else

    typedef signed char     int8_t;
    typedef short           int16_t;
    typedef int             int32_t;

#ifndef __intptr_t_defined
    typedef long            intptr_t;
#define __intptr_t_defined
#endif

    typedef unsigned char   uint8_t;
    typedef unsigned short  uint16_t;
    typedef unsigned int    uint32_t;

    typedef unsigned long   uintptr_t;

#endif

#endif  /* S4_FIXED_H */
