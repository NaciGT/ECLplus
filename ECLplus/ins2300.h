#pragma once
#include "ECLplus.h"

enum INS2300 {
    INS_SET_DIFFICULTY = 2300,   // 直接修改当前难度，参数：1 个整数
    INS_SET_CUSTOM_VAR = 2301,   // 新增：设置自定义永久变量，参数：索引(0~29), 值
};

/* INS_2300 series: difficulty control & custom variables */
BOOL ins_2300(ENEMY* enm, INSTR* ins);