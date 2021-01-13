/*
 * ismounted.h --- declare function(s) that check if a device is mounted.
 * Linux-specific.
 */

#ifndef _ISMOUNTED_H
#define _ISMOUNTED_H 1

#include <stdbool.h>

extern bool ismounted(const char *device);

#endif /* _ISMOUNTED_H */
