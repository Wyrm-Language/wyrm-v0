#ifndef WYRM_WORK_AREA_H_
#define WYRM_WORK_AREA_H_

#include <wyrm/primitive.h>

WYRM_BEGIN_DECLS

#define WYRM_WORK_AREA_LEN 8

/**
 * Generic 'User Data' Friendly Field
 *
 * A small working space intended for temporary stack parameters and type
 * erased operations. Work areas should be tightly coupled to a single known
 * API usage.
 */
typedef struct wy_work_area
{
    wyrm_primitive data[WYRM_WORK_AREA_LEN];
} wy_work_area;

WYRM_END_DECLS

#endif
