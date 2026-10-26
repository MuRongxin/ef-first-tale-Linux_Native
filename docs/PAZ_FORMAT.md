# PAZ 容器格式 — minori 引擎（`ef - the first tale`，英文 Steam 版）

状态：**已完整逆向并验证**，覆盖本安装中的全部 13 个 `.paz` 文件。
本文档按「干净房间（clean-room）Linux 移植」的实现规格来撰写；本目录下的 `paz.py`
是参考实现（唯一依赖：用于 Blowfish 的 `pycryptodome`）。

逆向证据与复现工具放在 `re_tools/` 目录
（密钥转储、exe 反汇编摘录、结构校验、抽取验证器）。

---

## 0. 移植速览（TL;DR）

```
0x00  char   game_id[32]        NUL 结尾；其余字节是打包器垃圾数据（忽略）
0x20  u32le  toc_len_obf
0x24  u8     toc[toc_len]       simple_key 异或混淆 + Blowfish-ECB 加密
...   u8     entry data area    条目数据区：8 字节对齐，偏移为绝对偏移
```

* `simple_key = toc_len_obf >> 24`；`toc_len = toc_len_obf ^ (simple_key * 0x01010101)`
  （本安装中全部 13 个归档的 `simple_key == 0`，即没有 XOR 混淆层）。
* TOC 用每个归档前缀各自的 32 字节密钥以 **Blowfish-ECB** 加密
  （前缀为 `bg`、`bgm`、`mov`、`scr`、`se`、`st`、`sys`、`voice`）。
* `ef` 使用的 Blowfish 是标准算法，但有一个怪癖：64 位**数据块按两个小端
  32 位字**处理（密钥字仍按大端）。
  配方：`plain = rev4( StandardBlowfish_ECB_decrypt( key = K,
  data = rev4(ciphertext) ) )`，其中 `rev4()` 表示在每个 4 字节组内部反转字节序。
* 条目负载（entry payload）处理：先 XOR `simple_key` → 用该前缀的 DATA 密钥做
  Blowfish-ECB 解密 →（仅针对未压缩、且文件名匹配 png/ogg/sc/avi/mpg 的条目，
  或 bgm/se/voice 归档）再用由文件名派生的密钥做逐条目 RC4 → 截断到
  `stored_size` → 若 `is_packed` 则做 zlib 解压（inflate）。
* `mov.paz` 的条目数据**不使用** Blowfish；它使用由 TOC 中的 seed 表与逐条目
  名称密钥派生的 256 字节 RC4 密钥。
* 本游戏版本的密钥见 §6。密钥来自游戏自己的 exe（在其中按字节取反存储）；
  r18_cn 归档使用同一套密钥。

---

## 1. 归档命名与密钥选择

归档文件名去掉 `.paz` 后转小写作为前缀，用于选择密钥对，取值为以下之一：

`bg bgm mov scr se st sys voice`（其他 minori 作品中还有 `pmbgm pmse pmvoice`）。

**游戏版本（game build）**决定 32 字节密钥集。本安装的 exe 是英文 Steam 版
（`ef_first_AA.exe`，版本资源 / RTTI / 内嵌密钥表三者一致）→ 对应密钥集
**“ef - the first tale (en steam)”**（§6.1）。
验证方法：在 exe 文件偏移 `0x1a7ae0..0x1a7d20` 找到了这些密钥字节（按字节取反后的形式），
随后成功解密了每一个 TOC。

`r18_cn/*.paz` 是 R18 补丁数据集；它们使用**同一套**密钥和同一格式。

引擎对多分卷归档的行为：它还会按顺序打开 `X.pazA`、`X.pazB`、……，
并把它们当作一个拼接后的连续流来处理（条目 offset 是该拼接流中的绝对偏移）。
本安装没有分卷文件，因此这条代码路径虽已实现但未实测。`paz.py` 支持该情况。

---

## 2. 磁盘布局

| 偏移 | 大小 | 类型 | 含义 |
|---|---|---|---|
| `0x000` | 32 | bytes | 游戏 ID 字符串，ASCII，NUL 结尾。观测值：`"ef_first_en_AA"`（根目录归档）、`"ef_first_en_AA_R18Patch"`（r18_cn）。NUL 之后的字节是**打包器未初始化的垃圾数据**（逐文件不同，引擎和所有已知工具都忽略它们）。 |
| `0x020` | 4 | u32 LE | `toc_len_obf` — 经过混淆的 TOC 字节长度 |
| `0x024` | `toc_len` | bytes | TOC（已混淆 + 已加密），`toc_len` 始终是 8 的倍数 |
| `0x024 + toc_len` | 剩余 | bytes | 条目数据区；每个条目都从一个绝对的、8 字节对齐的文件偏移开始 |

头部解析（参见 `paz.py: PazArchive._read_header`）：

```
v          = u32le @ 0x20
simple_key = v >> 24                      # 单个字节（此处 13 个文件中全为 0）
toc_len    = v ^ (simple_key * 0x01010101) if simple_key else v
assert toc_len % 8 == 0 and 0x24 + toc_len <= file_size
data_start = 0x24 + toc_len
```

引擎自身的读取代码（`ef_first_AA.exe` @ 0x52E990）先读 0x20 字节头部，
再读 4 字节，然后读 TOC 缓冲区，并调用其“解密钩子” @ 0x435390 ——
也就是下文的 Blowfish-ECB 解密。参考抽取工具（`Inori/FuckGalEngine
Minori/main.cpp`）展示了相同布局：`struct PAZHDR { u8 unknown[32]; u32
toc_len; }`，并以 `hdr.toc_len >> 24` 推导字节混淆密钥。

**实测头部数值**（全部 `simple_key = 0x00`）：

| 归档 | game_id | toc_len | 文件大小 | data_start | 条目数 |
|---|---|---:|---:|---:|---:|
| bg.paz | ef_first_en_AA | 143128 (0x22f18) | 1272342060 | 143164 | 3457 |
| bgm.paz | ef_first_en_AA | 1096 (0x448) | 51733692 | 1132 | 29 |
| mov.paz | ef_first_en_AA | 424 (0x1a8) | 51003452 | 460 | 4 |
| scr.paz | ef_first_en_AA | 2576 (0xa10) | 405268 | 2612 | 75 |
| se.paz | ef_first_en_AA | 3640 (0xe38) | 6494884 | 3676 | 89 |
| st.paz | ef_first_en_AA | 43672 (0xaa98) | 121894852 | 43708 | 1033 |
| sys.paz | ef_first_en_AA | 24936 (0x6168) | 20932372 | 24972 | 572 |
| voice.paz | ef_first_en_AA | 233216 (0x38f00) | 217589044 | 233252 | 5830 |
| r18_cn/bg.paz | ef_first_en_AA_R18Patch | 35848 (0x8c08) | 288256132 | 35884 | 868 |
| r18_cn/scr.paz | ef_first_en_AA_R18Patch | 680 (0x2a8) | 79868 | 716 | 19 |
| r18_cn/st.paz | ef_first_en_AA_R18Patch | 88 (0x58) | 125180 | 124 | 2 |
| r18_cn/sys.paz | ef_first_en_AA_R18Patch | 2896 (0xb50) | 1540276 | 2932 | 63 |
| r18_cn/voice.paz | ef_first_en_AA_R18Patch | 9936 (0x26d0) | 13178484 | 9972 | 248 |

**头部 ID 字段说明。** 这 32 字节字段是一个 NUL 结尾的字符串；NUL 之后的字节是
打包器遗留的内存内容。证据：根目录归档包含 `"ef_first_en_AA"`（14 个字符）+
位于 0x0e 的 NUL + 17 个逐文件变化的垃圾字节；r18 归档包含
`"ef_first_en_AA_R18Patch"`（23 个字符）+ 位于 0x17 的 NUL + 8 个逐文件变化的
垃圾字节。所有解密/解析都忽略这些字节。引擎显然只把这个字符串当作
游戏/版本标识符使用。

---

## 3. 字节混淆层（简单的 XOR）

引擎可以选择用同一个字节对整个头部 + TOC + 数据区做 XOR，该字节由
*存储形式*的 TOC 长度推导而来：

```
simple_key = toc_len_obf >> 24          # 本安装中每个归档都是 0x00
```

对磁盘上的 TOC 做解码（依据参考抽取工具和引擎的
`read_unobfuscate` @ 0x52D7F0）：

```
blob = file[0x24 : 0x24 + toc_len]
if simple_key:
    blob[i] ^= simple_key   for every i          # 单字节密钥
```

（在引擎中，密钥推导完成后，同一步骤也会作用于 36 字节的头部以及每个条目负载。）
由于本处 13 个文件的 `simple_key == 0`，该层实际是空操作（no-op）；非零路径未实测。
注意：你在 0x20 处读到的值本身已经被 XOR 过，这就是为什么要从它的高字节取密钥：
只要 `toc_len < 2^24`（本处成立：最大 233216），就有
`stored_high_byte = (toc_len>>24) ^ simple_key = simple_key`。

---

## 4. Blowfish 变体（关键细节）

游戏使用**标准 Blowfish** 分组密码（Bruce Schneier 的算法，16 轮，P 数组 + 4 个
S 盒 = 标准的 π 数字常量；这些常量存在于 exe 的 `.rdata` 文件偏移 0x170dd8 处），
以 **ECB 模式、无 IV、无填充**工作，密钥长度为 **32 字节**，并具有以下两个
字节序约定：

* **密钥材料（key material）**：标准 Blowfish 密钥编排，即 32 个密钥字节按
  4 字节一组、以**大端**方式解释（与参考测试向量一致）；
* **数据块（data block）**：8 个密文字节被装载为两个**小端** u32 字
  （x86 原生装载方式）。等价地说，相对于通常的大端约定，数据字被反转了字节序。

原因：引擎的解密函数（`ef_first_AA.exe` VA 0x435393，密钥编排位于
0x435040）执行 `mov ecx,[edx]`，然后按
`S0[byte3] + S1[byte2] ^ S2[byte1] + S3[byte0]` 索引 S 盒，即对小端字应用标准
F() 函数；而密钥编排按 `key[j]<<24 | key[j+1]<<16 | …`
（大端）组装密钥字。这种“半移植”的错配实现，正是原始 `exf2paz` / FuckGalEngine
的 `blowfish.cpp` 所复现的行为，GARbro 的 C# `Blowfish` 类也表现相同。

### 可移植配方（推荐：委托给任意标准 Blowfish 实现）

```
def rev4(b):                    # 在每个 4 字节组内部反转字节序
    return b''.join(b[i:i+4][::-1] for i in range(0,len(b),4))

def paz_blowfish_decrypt(key32, ciphertext):      # 长度 % 8 == 0，ECB
    assert len(ciphertext) % 8 == 0
    return rev4( std_blowfish_ecb_decrypt(key32, rev4(ciphertext)) )
```

`std_blowfish_ecb_decrypt` 可以是 OpenSSL `BF-ECB`（legacy provider）、Crypto++、
pycryptodome（`Crypto.Cipher.Blowfish.MODE_ECB`）、libtomcrypt，或任何能通过
标准大端测试向量的 Blowfish 实现。TOC 和条目数据都使用同一个函数，只是密钥不同。

### 自包含伪代码（如果你不想引入 Blowfish 依赖）

标准 Blowfish，只需替换以下两处：

```
/* 密钥编排：从密钥字节构造 P[0..17]，每字 4 字节，大端（BIG-endian） */
data = (key[j]<<24) | (key[j+1]<<16) | (key[j+2]<<8) | key[j+3];  P[i] ^= data

/* 数据块装载/存储：小端（LITTLE-endian）字 */
L = b[0] | b[1]<<8 | b[2]<<16 | b[3]<<24
R = b[4] | b[5]<<8 | b[6]<<16 | b[7]<<24
... 标准 16 轮 Feistel，按解密顺序（P[17], P[16..1], P[0]） ...
```

等价地说：其它地方全都保持标准大端约定，但把**输入密文和输出明文**的每个
4 字节组都反转字节序，并在密钥编排之前把密钥的每个 4 字节组反转字节序。
（**不要**使用 CBC：这里既没有链式（chaining），也没有 IV。）

已验证：用该方法解密 `scr.paz` 的 TOC 得到 `count = 75`，第一个文件名是
`test.sc`；全部 13 个归档都解密出合理且完全自洽的 TOC。

---

## 5. TOC 结构

解密后 TOC 的明文结构（所有整数为小端）：

```
u32  entry_count
if archive prefix == "mov":
    u8  seed[256]                    # MOV 条目加密所用的 seed 表
repeat entry_count times:
    char name[]                      # NUL 结尾；CP932/Shift-JIS，实际为 ASCII
    u64  offset                      # 在（拼接后的）归档中的绝对偏移
    u32  original_size               # 解包后的大小
    u32  stored_size                 # 负载大小：压缩条目为压缩后大小，否则为原始大小
    u32  aligned_size                # 磁盘上的负载大小 = align8(stored_size)
    u32  is_packed                   # 0 = 原始数据，1 = zlib 压缩
u8  zero_padding[]                   # 补齐到 toc_len；toc_len 是去混淆后的 toc_len_obf，8 字节对齐
```

`offset` 是绝对偏移（`0x24 + toc_len + relative`），即第一个条目的偏移
等于 `data_start`。

**结构性不变式（已对全部 13 个归档验证，见
`re_tools/toc_check.json`）：**

1. `toc_len == align8(4 + (256 if mov else 0) + Σ_i (len(name_i) + 1 + 24))`
2. `min(entry.offset) == 0x24 + toc_len`（数据紧跟在 TOC 之后开始）
3. `max(entry.offset + entry.aligned_size) == file_size`（没有空隙，没有尾部数据）
4. 每个 `aligned_size % 8 == 0`，且 `stored_size <= aligned_size`，`is_packed ∈ {0,1}`

给需要与 IDA 输出对照的移植者：24 字节记录可以看作
`u32 a, b, c, d, e, f`，其中 `(a,b)` 是 64 位 offset 的低/高半部分，
而 `c,d,e,f = original_size, stored_size, aligned_size, is_packed`。

引擎中的文件名是 CP932（Shift-JIS）编码；在这些归档里它们都是纯 ASCII。
`voice.paz` 的条目**没有文件扩展名**（它们是 Ogg Vorbis 音频，可嗅探
`OggS`）。有些文件名故意包含 `.png-`、`.png.tmp` 或反斜杠目录分隔符，
例如 `thumb\th_216_05.png`。

---

## 6. 密钥（本游戏版本）

密钥每个 32 字节，按归档前缀各有一对：**TOC 密钥**（解密 TOC）与
**DATA 密钥**（解密条目负载）。引擎把它们以**按字节取反（每字节取二补数，
byte-negated / two's-complement per byte）**的形式存放在自己的 exe 中：对下表而言
`exe_byte = (~real_byte) & 0xFF`，位于 `.data` 中文件偏移
0x1a7ae0 + 32·k 处。GARbro 与 FuckGalEngine 以取反后的正值形式列出相同的密钥。

### 6.1 `ef - the first tale (en steam)` —— **本安装使用的密钥集**

类型口令（type password，即 “crud”）：`png = a7b77qgx`、`ogg = 52AUwF8B`、`sc = 102ycz1s`、`avi = qmzj4Y9C`。

| 前缀 | TOC 密钥（hex） | DATA 密钥（hex） |
|---|---|---|
| `bg` | `8c7919517e51a0fd8348dc85cb68858e68905bd7d5b96336ea3699c15b683f83` | `ca3c7817e1c631e5f88de7aff702a3697cb9d5e337e54eda45546c2910c75fde` |
| `bgm` | `78e52f7768fa49ea1b0ef53f7b38014a835bc8c948eb78d0ab2688b85d46f307` | `8b7731e7aa9dae4c1f216750c32b9d395794b257c353a3c9dfa880733026d32b` |
| `mov` | `5a4caf2034cc9510780ce2b9bae97b9c2026274e7fdd36c58d10325f0337e1ea` | `5a4caf2034cc9510780ce2b9bae97b9c2026274e7fdd36c58d10325f0337e1ea`² |
| `scr` | `72634e755d2c980b205f82828bde4379f37bd2b66d4390b6436a4d9f8389ca5a` | `b7d2955cf7a0d3ecc20ccbf887a8c1b6d498ecf5f9b8361e897e4dbc7bad1465` |
| `st` | `b891bb7b469812fe98cc267bf1356131c694d340d79d8cd10db45c5d81c44a9c` | `630bdf10c9e57ef99126b50476f59800240cd1b3766a1416e31ebfee27694f93` |
| `sys` | `0112c9880d9dd23a0573bac209226d0e80a8ae1ef8b33a76494962531bd1110b` | `fd399525e2b2762ff9bc115dd145f6eb9e6de3574574d04bcd21146be6537951` |
| `voice` | `2fd5d774760b34126f7c3e964fe6ca4cfcdf6a730b891cb1618335270feb79df` | `151694fdcf2ce124a14e6d1e46e1a7b17395c0cc172da47ca3ad8cae42043258` |
| `se` | `c3011a3ed51fcd2bd3904dd1772e469709b10d5dc0cb4652cff5b3a57cde9038` | `3dca17d9518de518087077409c8bcb396a6be53da76e650238e09905096d1aa8` |

² `mov` 的 DATA 密钥不被引擎使用（MOV 使用 §8.3 的 RC4 方案）；表中只是重复了
TOC 密钥。`r18_cn/*.paz` 使用这同一套密钥。

同一引擎家族中的其他 minori 作品/版本使用不同的密钥集；从参考工具提取出的
完整 23 部作品密钥表位于 `re_tools/reference_keys_all23games.json`
（包含 `ef - the first tale (en)` 与 `(jp)` 变体，以备将来移植不同发行版时使用）。

---

## 7. 条目数据 —— 普通归档（除 `mov` 之外的所有归档）

```
def read_entry(e):
    buf = read_file_range(e.offset, e.aligned_size)      # 如有 .pazA.. 分卷则跨卷读取
    if simple_key: buf ^= simple_key                    # 本处为空操作
    buf = paz_blowfish_decrypt(DATA_KEY[prefix], buf)   # 对整个对齐缓冲区解密

    pw = ""
    if e.is_packed == 0:                                # 逐条目 RC4 仅用于原始（未压缩）条目
        pw = type_password(e.name, prefix)              # 见下方规则

    if pw:
        key = (e.name.lower() + " " + "%08X" % e.stored_size + " " + pw).encode("cp932")
        buf = rc4_standard(buf, key)                    # 标准 RC4，keystream 从 0 开始

    payload = buf[0:e.stored_size]
    if e.is_packed:
        out = zlib_inflate(payload)                     # 标准 zlib，例如开头为 78 9C
        assert len(out) == e.original_size
        return out
    assert len(payload) == e.original_size
    return payload


def type_password(name, prefix):                        # 引擎的精确规则
    low = name.lower()
    if ".png" in low:                     return png_pw
    elif ".ogg" in low or prefix in {"bgm","se","voice"}: return ogg_pw
    elif ".sc" in low:                    return sc_pw
    elif ".avi" in low or ".mpg" in low:  return avi_pw
    else:                                 return ""
```

注意事项：

* 该判断是对**原始大小写**文件名做**子串**匹配（引擎使用大小写敏感的 `find`）；
  而 RC4 种子串使用**小写化**后的文件名。
  `%08X` 是 *stored_size*（等价于解密后的大小；对原始条目来说它等于 `original_size`）。
* RC4 密钥是 `"<小写文件名> <8 位大写十六进制数字> <类型口令>"`
  的 CP932/ASCII 字节串——注意其中的单个空格。
* `zlib` 流是标准 zlib（RFC1950，开头为 `78 9C`；压缩级别 9）。
  游戏的打包器只压缩*部分*文件（脚本、`.ani`、部分 `.png`）；
  各归档的压缩计数见 EXTRACTION_REPORT.md。
* **已知怪癖**：在 `st.paz` 和 `r18_cn/st.paz` 中，每个被压缩的 `.ani` 条目
  解压后为 `original_size + 8` 字节，且最后 8 个字节为 `00`。引擎
  （以及原始打包器）只消费 `original_size` 字节。正确的抽取器必须截断到
  `original_size`（参考实现 `paz.py` 会记录该调整）——已验证
  `st.paz` 506/506 个压缩条目以及 r18 归档 1/1 个条目；其余所有压缩条目
  解压后都精确等于 `original_size`。
* 在所有观测到的文件中，`stored_size` 之后的对齐填充解密后全为零。

### 7.1 可选的 `simple_key` 字节 —— 同样作用于负载

引擎源码显示，负载在 Blowfish 步骤之前也会与 `simple_key` 做 XOR
（`paz.py` 已实现；本安装中为空操作）。

---

## 8. 条目数据 —— `mov.paz`（特例）

`mov.paz` 是*唯一*其条目数据不使用 Blowfish 的归档。TOC 在 `entry_count`
之后紧接着额外包含一个 256 字节的 `seed` 表；条目按如下方式处理：

```
seed2 = (e.name.lower() + " " + "%08X" % e.stored_size + " " + type_password).encode("cp932")
rc4_key[i] = seed[i] ^ seed2[i % len(seed2)]      # i = 0..255
ks         = rc4_standard_keystream(rc4_key, 0x10000)   # 对 0x10000 个零字节运行 RC4 得到 keystream
for k in range(e.aligned_size):
    buf[k] ^= ks[k % 0x10000]
payload = buf[0:e.stored_size]
# 本作全部 4 个 MOV 条目的 is_packed == 0；若该标志被置位，则照常做 zlib
```

TOC 中的 seed 与条目密钥串的构造方式与 FuckGalEngine 的
`decrypt_mov()` / GARbro 的 `MovPazArchive` 完全一致。本作全部 4 个 MOV 条目
都未压缩（`is_packed = 0`），内容为 MPEG-PS 或 AVI 文件。

---

## 9. 各区域的混淆/加密汇总

| 区域 | 变换（按顺序） |
|---|---|
| 整个 头部+TOC+数据 | 单字节 `simple_key` 的 XOR（来自存储的 TOC 长度高字节；本处为 0） |
| TOC | Blowfish-ECB，TOC 密钥，32 字节密钥；`toc_len` 为 8 的倍数 |
| 条目负载 | Blowfish-ECB，DATA 密钥，作用于 `aligned_size` 字节（`mov` 除外） |
| 条目负载，文件名含 png/ogg/sc/avi/mpg 的原始条目（或音频归档） | 使用 名称/大小/口令 派生密钥的 RC4 |
| `mov` 条目 | 256 字节 RC4 密钥流（seed⊕名称密钥），0x10000 字节 keystream XOR |
| 压缩条目 | 对前 `stored_size` 字节做 zlib inflate |

这里没有 LZ77 或其他自定义压缩。exe 中的 `0x435390`/`0x435393` 是
Blowfish 解密例程（在 IDA 中看到的“可选解压钩子”是解密，不是解压）；
zlib 解压则是每个压缩条目单独调用一次 Crypto++ 的 `ZlibDecompressor`。

---

## 10. `r18_cn/` 覆盖集

* 相同的容器格式和相同的密钥；`game_id = "ef_first_en_AA_R18Patch"`。
* 它是一个*次级数据集*（由 R18 补丁安装），不是完整拷贝：
  bg/scr/st/sys/voice 分别为 868/19/2/63/248 个条目。引擎的 exe 中有注册表
  标志 `SOFTWARE\minori\ef_first_en_r18patch`；补丁目录会在基础归档之前被搜索
  （或与基础归档按文件名合并）。内容包括：新的 R18 CG 与
  立绘（`evh*`、`ev*`、`st_*`）、名称表、6 个新脚本和 13 个
  被覆盖的脚本、248 个新语音文件、59 个新 UI 缩略图 + 4 个覆盖性的
  `MemoriesSelect*` 图像。
* `r18_cn/bg.paz` 包含两个纯文本文件（它们也是标准 PAZ 条目）：
  * `names.txt` — CP932/Shift-JIS 表，把立绘 ID 映射到日文
    名称，例如 `st_miyako<TAB>みやこ`；
  * `log.txt` — 原始打包机器上遗留的一份 2011 年 `dir` 目录列表
    （源 PSD 文件名/大小/时间戳）——是打包器输入内容的证据。

---

## 11. 交叉实现确认

| 来源 | 贡献内容 | 验证方式 |
|---|---|---|
| 本安装中的文件 | 基准事实（ground truth） | 全部 13 个 TOC 解密成功；§5 的结构性不变式精确成立 |
| `ef_first_AA.exe`（本游戏） | `.data` 中的密钥表（按字节取反）、`.rdata` @0x170dd8 的 Blowfish 表、Blowfish 解密 @VA 0x435393、密钥编排 @VA 0x435040、PAZ 打开流程 @VA 0x52E990、`.paz` 字符串 @VA 0x55ffbc | 取反后密钥与 §6 一致；解密函数的字节序解释了所需的 `rev4` 变换；ECB 循环没有链式 |
| GARbro `ArcFormats/Musica/ArcPAZ.cs`（morkt） | 相同的头部/TOC/管线语义；GARbro 的 `Blowfish.cs` 复现了相同的字序怪癖 | 算法一致 |
| FuckGalEngine `Minori/Minori/fuckpaz/main.cpp` + `blowfish.cpp`/`blowfish.h2`（asmodean/Fuyin/Inori） | `PAZHDR`/`PAZENTRY` 结构、23 部 minori 作品的密钥表、`unobfuscate3`=RC4、`decrypt_mov`、zlib 处理 | 密钥表数值复现了 exe 中 “en steam” 条目的取反字节；抽取结果一致 |

`re_tools/` 中包含：参考密钥 JSON、exe 密钥表 hexdump、
Blowfish ECB 循环的反汇编摘录、脚本命令 token 转储、
TOC 结构校验结果，以及抽取验证器。

---

## 12. 未知项 / 未验证的边角（诚实清单）

1. `simple_key != 0` 路径是**推断**自引擎/参考工具源码
   （对头部 + TOC + 数据做 XOR）；此处每个文件的 `simple_key == 0`，因此
   未实测。可以实现它，但若将来某个归档需要别样的处理，也不必惊讶。
2. 32 字节头部游戏 ID 字符串的确切语义（引擎在何处使用它）。
3. 多分卷 `.pazA/.pazB` 归档：引擎支持；本处不存在，因此
   未实测（`paz.py` 中已实现）。
4. 参考工具中 `version >= 2` 的 RC4 “skip rounds” 机制
   （`skip = (crc32(name) >> 12) & 0xFF`，在 `not` 取反的密钥字节之后）**不**适用于
   本游戏（`version == 1`），因此未验证。
5. 引擎是否对同名文件给予 r18_cn 文件优先权（合并顺序）——由内容推断；未在运行时观测。
6. `.ani` 内部结构、脚本 VM 语义、PSD 在引擎中的作用——超出
   容器格式范围（见 EXTRACTION_REPORT.md）。

---

## 13. 移植检查清单

1. 读取头部，解码 `toc_len`（8 字节对齐），读取 TOC 数据块，用 `simple_key` 做 XOR。
2. 实现/链接一个标准 Blowfish-ECB 解密；在其输入
   和输出上套用 `rev4()`（或直接写小端字变体）。
3. 用该归档前缀对应的 TOC 密钥解密 TOC；按 §5 解析。
4. 对每个条目：在绝对 `offset` 处读取 `aligned_size` 字节；XOR
   `simple_key`；用 DATA 密钥做 Blowfish-ECB 解密；若归档是 `mov` 则改用 §8 方案；
   若未压缩且文件名/口令规则匹配，则按 §7 的种子串做 RC4；截断到
   `stored_size`；若 `is_packed` 则 inflate（对 `st.paz/*.ani` 的结果再截断 8 字节）。
5. 校验：第一个条目 offset == `0x24 + toc_len`；最后一个条目 end == 文件大小。若不符，
   说明密钥集或字节序有误。
