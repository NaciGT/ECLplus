#pragma once
/* 音乐室解锁状态持久化（v2）
   - 我们自己维护一份 128 位解锁位图（文件 ECLplusMusicUnlock.txt，与 ECLplus.ini 同目录 ✓）
   - 启动后约 2 秒（存档已载入）把位图写进游戏的解锁数组 ✓
   - 之后每秒把数组里"新出现的已解锁"合并回位图并落盘 ✓（只增不减，绝不与游戏抢写 ✓）
   - 热键 F8：立即把位图重写进数组（读档后可手动恢复 ✓）
   - 解锁数组位置：ScoreFile 对象 [0x4B77DC] + 运行时偏移（0x45ADF7 → 0x2D16E / 0xF016E ✓）*/
void MusicLockTick(void);      /* 每帧调用，内部自节流 ✓ */
void MusicLockApplyNow(void);  /* 立即把位图写入数组 ✓ */
void MusicLockInit(void);      /* 读取位图文件 ✓ */void MusicLockUnlockRange(int from1, int to1);   /* 解锁 [from,to]（曲号 1 起）并落盘 ✓ */
