/* SPDX-License-Identifier: GPL-3.0-only */
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Mapper.h"
#include "K6502.h"

struct MapperTable_tag MapperTable[] = {
    {0, Map0_Init}, {1, Map1_Init}, {2, Map2_Init},   {3, Map3_Init},
    {4, Map4_Init}, {7, Map7_Init}, {66, Map66_Init}, {-1, NULL},
};

#include "mapper/InfoNES_Mapper_000.cpp"
#include "mapper/InfoNES_Mapper_001.cpp"
#include "mapper/InfoNES_Mapper_002.cpp"
#include "mapper/InfoNES_Mapper_003.cpp"
#include "mapper/InfoNES_Mapper_004.cpp"
#include "mapper/InfoNES_Mapper_007.cpp"
#include "mapper/InfoNES_Mapper_066.cpp"
