#pragma once
/* 存档完整性检查（启动一次 ✓）+ 通用诊断打印
   ★ 输出走 ECLplus 控制台（EclPrint ✓，需要 DEV 构建：Release-dev ✓），同时写日志 ✓
   ini: SaveCheckShow=1（0 = 只写日志 ✓） */
void SaveCheckInit(void);
void SaveCheckRun(void);    /* 立即检查一次（热键 `；` ✓） */
void SaveCheckFreeze(void);
void SaveCheckMem(void);    /* 检查/转储【内存中的 ScoreFile】（解密后的明文 ✓，能看出真实损坏 ✓） */ /* 把当前存档原样复制一份（热键 `'` ✓，用于抓现行 ✓） */    /* 立即检查一次（热键 `；` 调用 ✓ 可反复运行 ✓） */
void EclPlusDiag(const char* fmt, ...);   /* 通用：把诊断信息打印到 ECLplus 控制台 ✓ */