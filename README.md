# ECLplus
ECLplus is a modification for Touhou 17 - Wily Beast and Weakest Creature. It's main goal is adding more functionalities for ECL scripts - this includes both custom instructions and custom variables.

---

## 本分支（fork）的额外改动

> 上游：Priw8 的 **ECLplus**。本仓库是它的一个分支，保留上游全部许可与实现，
> 在此基础上增加了下列功能。英语说明见文件后半部分（上游原文未改动）。
>
> 本分支面向 **《东方旧约集》**（th17 改版）开发；通用部分其它 th17 改版也可直接用。

### ⚠️ 免责声明

- 本仓库**不包含任何东方 Project 游戏资源**（`.anm` / `.ecl` / `.dat` / 音频 / 文本等一律不含）。
- 使用本 DLL **必须自备正版《東方鬼形獣》（th17 v1.00b）**。
- 所有补丁都带**原始字节校验**（`expected` 不符则整批放弃），不会在版本不匹配的 exe 上强行写入。
- 修改存档类功能有风险，请自行备份 `thow.dat`。
- **仅供学习交流使用，请勿用于商业用途。**

### 新增功能

| 模块 | 位置 | 说明 |
|---|---|---|
| **符卡栏位 512 扩容** | `score*.cpp` `score_table.h` | 把游戏的符卡栏位从 101 扩到 512（154 处补丁，每条带原值校验）。含记录步长、名字偏移、折叠常量三类站点的完整修正 |
| **符卡表自建界面** | `score_ui.cpp` `score_gui.cpp` `score_draw.cpp` | 战绩页符卡列表改为自建表（v9 自绘 / ImGui 两套），读 `spell_table.csv`（格式见 `ECLplus/spell_table.example.csv`）。文件名可由 ini 的 `SpellTableFile` 指定，换表不用改名 |
| **符卡名打码** | `score_ui.cpp` | 未遭遇的符卡不显示真名（`GuiMask*` 系列开关） |
| **索引条** | `score_index.cpp` | 表格上方的 `第 N 页 / 共 M` 独立单元，位置/字体/颜色/描边全部 ini 可调 |
| **音乐室扩容 + 解锁** | `music99.cpp` `musiclock.cpp` | 菜单/曲目列表上限扩到 **99**（突破原版 18 轨：音轨表 / Menu 缓冲 / 初始化 10 帧三处）。**但存档里的「已解锁曲目」位图只有 42 字节**（`ScoreFile+0x2D16E`，512 布局 `0xF016E`，索引 0..41）—— 紧随其后的 `+0x2D198`（`0xF0198`）是**成就位数组**，写入越界就会把成就全部刷开（这正是历史 bug 的根因）。所以代码把写入范围严格夹在 `cap=42` 内；自己的位图另存 `ECLplusMusicUnlock.txt`（上限 64，实际生效 42） |
| **随机截屏** | `snap.cpp` | 游玩中随机截图到 `snapshot\`，可热键手动 |
| **存档完整性检查** | `savecheck.cpp` | 启动自动检查 + 热键；结果打印到控制台并写日志 |
| **变量控制（2300 系）** | `ins2300.cpp` | `ins_2300(rank)` 直接改当前难度（写 `th17.exe+0xB5A00`，即 ECL 整数全局 `-9959`；0=Easy…4=Extra）。`ins_2301(index, value)` 写 ECLplus 自建的 50 个永久变量（存自己的文件，**不写游戏存档**） |
| **文本生成器（intvar）** | `intvar.cpp` | 变量等于某个触发值时，把对应文本导出成 UTF-8 文件（含"未解锁"提示分支），实现"同一位置按条件显示不同文本"。示例条目见 `s_entries_LastWord[]`，可自行替换 |
| **画线（2400 系）** | `ins2400.cpp` `binhack.cpp` | `ins_2400(color, alpha)` / `ins_2401(layer)` / `ins_2402(x1,y1,x2,y2)`：用游戏自己的 D3D9 在两个坐标间画线，256 条缓冲，分 BACK/FRONT 两个通道，挂进绘制任务表。ECL→屏幕映射：`screen = (2*x+448, 2*y+32)` @1280x960 |
| **BOSS 位置标记（BossMarker）** | `ECLplus.cpp` | 给 0/1 号 BOSS 各生成一个跟随其 x 的 `front.anm` 标记（详见下节） |

### BossMarker 说明

原版 `front.anm` 的 `script112`（那个 "enemy" 标签）**整个舞台只有一个**，位置来自调用点残留的 `xmm0`，不跟随任何 BOSS。本分支改成每个 BOSS 槽各一个独立 VM。

实现要点（全部经反汇编与实测确认）：

```
layer 29 → 绘制任务表 id 78 → 在 mode 2（屏幕/UI 坐标系）里绘制
           ui_x = 640 + 2 * ecl_x
           ui_y = 480 + 2 * ecl_y
```

- 位置：`x = 2 * BOSS.x + 449`、`y = 960`（引擎自身在 `Gui::on_tick` @ `0x42C587` 用的是 `2*BOSS.x + 448`、`y = 960.0f`）
- 透明度：`|BOSS.x - player.x| < 64` 时 alpha 由 `0x40` 线性升到 `0xFF`；`|BOSS.x| > 192` 时 alpha = 0（照抄引擎同段逻辑）
- 闪烁：受击后持续约 20 帧、血量低于 30% 时持续
- 暂停冻结：读 `GAME_THREAD + 0x8C` 的 `bit 0x10`（`sub_4446E0/4447E0/444980` 置位，`sub_444BF0` 清除，`GameThread::on_tick` 每帧检查）
- 注销延迟：`ins_512` 清槽后延迟约 30 帧再移除，避免死亡演出期间瞬断
- 全部参数在 `ECLplus.ini` 的 `[ECLplus] BossMarker*` 段

### 热键

> ⚠️ **下面 6 个调试热键只在 DEV 构建（`Release-dev` / `Debug-dev`）里编译进去**，由 `#ifdef DEV` 包住；正式 `Release` 构建里这些按键完全不存在，不会误触。
> 只有 `X` / `Esc`（收起界面）是所有构建都有的用户功能。

游戏运行时全局生效（挂在窗口消息处理里，不占游戏自己的按键）：

| 键 | 功能 | 构建 |
|---|---|---|
| `F7` | 手动截图（存 `<游戏目录>\snapshot\thow_XXX.bmp`） | DEV |
| `F8` | 立刻把解锁位图重写进存档（音乐室） | DEV |
| `F9` | 解锁曲目 21–28 | DEV |
| `F10` | 解锁全部曲目（上界受存档容量 42 限制） | DEV |
| `;` 分号 | 随时检查存档完整性，结果打印到控制台并写日志 | DEV |
| `'` 单引号 | 冻结当前存档现场（排查用） | DEV |
| `X` / `Esc` | 立刻收起自建符卡表界面 | 全部 |

### ini 开关总览

全部 101 个键及中文注释见 `ECLplus/ECLplus.ini.example`。按模块分组：

| 前缀 | 数量 | 作用 |
|---|---|---|
| `SpellSlots` / `Force` / `MusicRoom99` | 3 | 512 扩容开关与强制安装、音乐室 99 轨 |
| `SpellTable*` | 31 | 自建符卡表：渲染路线（`Render`/`Draw`/`Gui`）、读表（`File`/`Init`/`Filter`/`Limit`）、排版（`X`/`Y0`/`DY`/`Align*`/`Font`）、诊断（`*Log`/`Probe`/`Monitor`/`Repair`） |
| `Gui*` | 16 | ImGui 主表窗口：位置尺寸、字体、文字色、`BgAlpha`（**0 = 完全透明**）、列数、描边、行距、滚动、`OwnView`、窗口消息接管 |
| `GuiMask*` | 3 | 符卡名打码：`Unknown`（未遭遇不显示真名）、`AnyRecord`（任一自机算解锁）、`NeedCap`（0=遭遇即算，1=必须取得） |
| `Draw*` | 15 | v10 手写渲染（已被 ImGui 取代，`SpellTableDraw=0` 时全部不生效） |
| `IndexBar*` | 14 | 索引条：位置、字号、颜色、底色与透明度、描边、分隔线 |
| `MusicUnlock*` | 1 | 音乐室解锁总开关 |
| `Snap*` | 3 | 随机截图：开关、最小/最大间隔秒 |
| `SaveCheck*` | 1 | 存档检查结果显示 |
| `BossMarker*` | 18 | BOSS 位置标记（见下节） |

### 构建

上游只有 `Release-dev|Win32` 带 `/utf-8`，其它配置会把源码里的中文按 GBK 解析而报 `常量中有换行符`（等于非 dev 配置根本构建不出来）。本分支已给四个 Win32 配置（`Debug` / `Debug-dev` / `Release` / `Release-dev`）**统一加上 `/utf-8`**，全部可构建；`DEV` 宏只决定"有没有调试热键"。推荐用 `Release-dev|Win32`：

```
MSBuild ECLplus\ECLplus.vcxproj /t:Rebuild /p:Configuration=Release-dev /p:Platform=Win32 /p:PlatformToolset=v142
```

### 致谢

- **Priw8** —— ECLplus 本体（2000/2100/2200 系指令、binhack 框架、imgui 界面）
- **thecl** —— 指令结构定义
- **exphp** —— th-re-data 的结构体与符号命名
- **Dear ImGui** —— MIT 许可的界面库

---

## Usage

### Installation
Put `ECLPLUS.dll` and `ECLplusLoader.exe` in the folder where `th17.exe` is located, and launch the game by using the loader. If you wish to use thcrap, you can change `th17.exe` in `games.js` to `ECLplusLoader.exe` and it will just work. If everything went correctly, a console with a message from ECLplus will start alongside the main game window (small note: with thcrap console enabled, both ECLplus and thcrap will end up outputting to the same console, and the ECLplus message will get burried under a massive amount of thcrap logs).

### Using custom instructions
In your ECL source file, `#include` the `ECLplus.tecl` script from the `ECLinclude` directory in order to get instruction names and definitios of ECLplus. Then, you can use the new instructions freely.

### Publishing modifications that use ECLplus
Simply including the content of the [LICENSE.txt](LICENSE.txt) file in the mod with some way of telling that it refers to ECLplus is fine (for example, in a file called `LICENSE_ECLplus.txt`).

## Instruction list
**ins_2000 series: various things that don't fall into a specific category**
- `msgf(string format, ...)` - shows a printf-formatted message box. Example: `msgf("Hello from ECL! My id is %d", _SS ID)` (note that since this uses the `D` param type for extra parameters, prefixing with typecast is necessary as of current thecl version).
- `printf(string format, ...)` - prints a printf-formatted string in the console, same concept as the instruction above.
- `cls()` - clears the console.
- `drawf(float x, float y, string format, ...)` - draws a printf-formatted string on the given coordinates in the game window (ECL coordinate system). Top-left corner of the string corresponds to the coordinates given. It will only be drawn for 1 frame, in order to keep it displayed all the time `drawf` needs to be in a loop.
- `drawColor(int c)` - sets color of strings drawn by `drawf`. Example: `drawColor(0xFFFF8800)` sets color to orange. Alpha is stored in the MSB, so that the order is ARGB; the example has an alpha value of `FF`, red value of `FF`, green value of `88` and blue value of `00`.
- `drawFont(int f)` - sets font of strings drawn by `drawf`.  The argument is the ID of an `ascii.anm` font.  Be aware that not all fonts support all printable ASCII characters, and the game may crash if the chosen font cannot render the given text.
- `drawAnchor(int h, int v)` - sets anchor point of strings drawn by `drawf`.  `h = 0/1/2` is center/left/right.  `v = 0/1/2` is center/top/bottom.  Default is `(1, 1)` (left/top). Globals `ANCHOR_CENTER`, `ANCHOR_LEFT`, `ANCHOR_RIGHT`, `ANCHOR_TOP` and `ANCHOR_BOTTOM` are provided for convenience.
- `drawShadow(int bool)` - enable or disable drop shadows for `drawf`.
- `drawReset()` - resets all modified attributes of `drawf` to their defaults.
- `playMusic(string name)` - replaces currently playing music with a new one. The name must be the same as in `thbgm.fmt` (e.g. `th17_06.wav`).
- `exit()` - unconditionally exits to the main menu (does not allow saving replays)
- `itemSpeed(float factor)` - set item falling speed factor, when the factor is smaller than `1.0f` the items automatically get the animation they have in LoLK. The game automatically increases the factor by `0.1f` every frame until it's back to `1.0f`.
- `spellSetCapture(int cap)` - change current spell's capture state, `0` will set bonus to "failed", any other value will allow capturing the spell. Does NOT set the ECL `CAPTURE` variable to 0.
- `spellSetBonus(int bonus)` - set both max spell bonus and current bonus of the current spell to the given value.
- `spellSetBonusNow(int bonus)` - set only the current bonus of the current spell. This means that the speed at which the bonus decreases will still be determined by the old max spell bonus value.
- `playSoundPos(int sound, float x)` - play a sound effect at the absolute position `x` (game coordinates); it will be panned accordingly.

**ins_2100 series: player manipulation**
- `playerPos(float x, float y)` - move the player to the given coordinates.
- `playerKill()` - immidiately kill the player.
- `playerBomb()` - immidiately make the player bomb (even if there are 0 bombs, though using this instruction does take away a bomb from the player).
- `playerSetLives(int lives)` - set the amount of lives to the given amount.
- `playerSetBombs(int bombs)` - set the amount of bombs to the given amount.
- `playerSetPower(int power)` - set player's power.
- `playerSetIframes(int frames)` - set player's invincibility frame count.
- `playerAllowShot(int state)` - disable/enable player's ability to shoot.
- `playerAllowBomb(int state)` - disable/enable player's ability to bomb.
- `playerSetHyperTimer(int time)` - manipulate the timer of an ongoing hyper.
- `playerSetDamageMult(float mult)` - set the player's damage multiplier.  When the factor is greater than `1f`, some visual effects from HSiFS's winter release will be enabled.
- `playerSetSpeedMult(float mult)` - set the player's movement speed multiplier.  When the factor is greater than `1f`, some visual effects from HSiFS's autumn release will be enabled. (but this variable is not entirely just a TH16 leftover; Marisa's bombs use it).  You must call this function every frame, as otherwise the game will reset it to `1f`.

**ins_2200 series: extended enemy intertaction**
A more in-depth explanation of the message system can be found [here](EnmMsg.md).
- `msgResetAll()` - fully resets the messages and frees any memory allocated for them.
- `msgReset(int channel)` - reset and remove the given channel.
- `msgSend(float a, float b, float c, float d, int channel)` - send message on the given channel.
- `msgReceive(intvar received, floatvar a, floatvar b, floatvar c, floatvar d, int channel)` - receive message from the given channel. The `a`-`d` variables will be set to the values from the message (if there are any messages to receive), `received` variable will be set to 1 if a message was received and to 0 otherwise.
- `msgPeek(intvar received, floatvar a, floatvar b, floatvar c, floatvar d, int channel)` - similar to `msgReceive`, except it doesn't actually receive the message (it stays on the list of messages in the channel). Can be used to check the message before actually deciding to receive it.
- `msgCheck(intvar receive, int channel)` - sets `receive` to 1 if there are any messages to be received in the given channel, 0 otherwise.
- `msgWait(int channel)` - wait until there is a message to be received in the given channel (actually an inline sub that uses `msgCheck` and not an instruction)

More detailed explanation of enemy IDs vs enemy iterators can be found [here](EnmIdIter.md).
In all following instructions, every time there is a variable parameter that gets set to some sort of return value, any non-variable parameter can be given to ignore the return value.
- `enmClosest(int &varId, float &varDist, float x, float y)` - sets `varId` to ID of the enemy that's closest to the given (`x`, `y`) coordinates, and `varDist` to the distance itself. Ignores enemies that are intangible (flag 32) or have no hurtbox (flag 1).
- `enmDamage(int id, int dmg [, int isBomb])` - unconditionally deal `dmg` damage to the enemy with the given ID. The third, optional parameter specifies whether the damage should be treated as bomb damage (that is, not get dealt when the enemy has bomb shield and get affected by the bomb damage multiplier).
- `enmDamageIter(int iter, int dmg, [, int isBomb])` - same as above, but uses an iterator instead of ID.
- `enmDamageRad(int &varCnt, float x, float y, float r, int maxCnt, int dmg [, int isBomb])` - deal `dmg` damage enemies within the circle with center at coordinates (`x`, `y`) and radius `r`. Maximum amount of enemies that can be damaged is determined by `maxCnt` (set to `-1` for unlimited amount), prioritizing damaging enemies closer to the center of the circle. The amount of the damaged enemies is written to `varCnt`. The optional `isBomb` works the same as in the previous instructions.
- `enmDamageRect(int &varCnt, float x, float y, float w, float h, int maxCnt, int dmg [, int isBomb])` - same as above, but damages a rect of width `w` and height `h` with center at coordinates (`x`, `y`) instead of a circle.
- `enmDamageRectRot(int &varCnt, float x, float y, float w, float h, float rot, int maxCnt, int dmg [, int isBomb])` - same as above, but the rectangle can be additionally rotated by `rot` radians.
- `enmIterate(int &varIterator, int prevIterator)` - used to iterate over the enemy list. If `prevIterator` is 0, then `varIterator` is set the the iterator of the first enemy. If it's not 0, then it must be a valid iterator, and in this case `varIterator` will be set to iterator of the enemy that's after `prevIterator`.
- `enmIdIter(int &varId, int iter)` - sets `varId` to ID value of enemy that iterator `iter` refers to.
- `enmIterId(int &varIter, int id)` - sets `varIter` to the iterator that refers to enemy with the given ID.
- `enmFlag(int &varFlag, int id)` - sets `varFlag` to flags of enemy with ID `id`.
- `enmFlagIter(int &varFlag, int iter)` - sets `varFlag` to flags of enemy that iterator `iter` refers to.
- `enmLife(int &varLife, int id)` - sets `varLife` to current HP of enemy with ID `id`.
- `enmLifeIter(int &varLife, int iter)` - sets `varLife` to current HP of enemy that iterator `iter` refers to.
- `enmPosIter(float &varX, float &varY, int iter)` - sets `varX` and `varY` to current coordiantes of enemy that iterator `iter` refers to.
- `enmBombInvuln(float &varInvuln, int id)` - sets `varInvuln` to bomb damage multiplier (set by `ins_565`) of enemy with ID `id`.
- `enmBombInvulnIter(float &varInvuln, int iter)` - sets `varInvuln` to damage multiplier (set by `ins_565`) of enemy that iterator `iter` refers to.

## Variable list
- `GI4` up to `GI7` - additational global integer variables, use them for whatever you want.
- `INPUT` - int, player input bitmask (works with replays). Writing to it has no effect.
- `SCORE` - int, score of the player (not including the last digit). Writable.
- `HIGHSCORE` - int, highscore of the player (not including the last digit). Writable.
- `BOMBING` - int, 1 if player is bombing, 0 otherwise. Not writable.
- `LIVES` - int, current amount of lives. Not writable, use `playerSetLives` instruction instead.
- `BOMBS` - int, current amount of bombs. Not writable, use `playerSetBombs` instruction instead.
- `GRAZE` - int, current amount of graze the player has. Writable.
- `PIV` - int, current point item value. Note that this value is larger than the one displayed on the screen because of how it's stored internally. Writable.
- `CONTINUES` - int, the amount of continues used so far (number displayed as last digit of the score). Writable.
- `CREDITS` - int, the amount of credits left (how many times can the player continue). Writable.
- `IFRAMES` - int, the amount of iframes the player has. Not writable, use `playerSetIframes` instruction instead.
- `PLSTATE` - int, player state. The following constants from `ECLplus.tecl` contain possible values: `PL_NORMAL`, `PL_DYING`, `PL_DEAD` and `PL_RESPAWN`. Writable (but just because you can doesn't mean that you should).
- `HYPERTIMER` - int, timer of a hyper. Not writable, use `playerSetHyperTimer` instruction instead.
- `DIALOG` - int, 0 if no dialog is active, nonzero if there is dialog active. Not writable.
- `SPELLBONUS` - int, current spell bonus. Returns `0` if there is no spell active.
