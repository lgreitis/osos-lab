/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VALUE_H
#define VALUE_H

#include "eapp.h"

struct value {
    uint32_t (*get)(void);
};

#endif
