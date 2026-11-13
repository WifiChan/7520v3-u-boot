# ZX297520V3 Web Failsafe U-Boot

基于 **U-Boot `2027.01-rc1`** 移植与定制，面向 **中兴微电子 (Sanechip) ZX297520V3** 平台（如 MF920U 等随身 Wi-Fi / CPE 设备），集成 **USB RNDIS 网卡、内置 DHCP 服务器、Web Failsafe 救砖与刷机界面、SPI NOR / SPI NAND 闪存读写校验、中兴分区表解析及 LZMA 自解压打包工具**。

> 原版上游 U-Boot 说明请参阅仓库根目录的 [`README`](README)。

---

## 核心特性

### 1. USB RNDIS 虚拟网卡与内置 DHCP
- **USB Gadget 参数**：`VID 0x0d00` / `PID 0x0721`，厂商字符串 `"Wifi Chan"`，支持 USB RNDIS 协议。
- **Web 管理地址**：默认 IP `192.168.72.1`（子网掩码 `255.255.255.0`），连接设备后浏览器访问 `http://192.168.72.1/` 即可进入管理界面。
- **内置 DHCP 服务器**（[`failsafe/dhcpd.c`](failsafe/dhcpd.c)）：自动为接入主机分配 `192.168.72.100 ~ 192.168.72.200` 网段 IP，支持 `DHCP_DISCOVER`、`DHCP_REQUEST` 及 `DHCP_DECLINE` 冲突处理，并遵循 RFC 2131 进行 300 字节最小报文填充。

### 2. 多方式触发 Web Failsafe
- **USB Boot 直通模式**：上电检测 `IRAM0` 标志位 `0x82002bc4`；当不等于 `0x12345678`（例如通过 `openloader` / `0.tloader` 等 USB 引导加载）时，跳过倒计时直接进入 Web Failsafe。
- **物理按键触发**：Flash 启动 3 秒倒计时内长按 Reset 按键（默认 `GPIO 52`）3 秒进入 Web Failsafe。
- **串口与环境变量触发**：串口输入 `f` 或 `w`，或设置环境变量 `bootmode=failsafe` / `failsafe_always=1`。
- **启动失败自动兜底**：正常执行 `bootcmd` 或引导原厂内核失败时，自动回退进入 Web Failsafe。

### 3. SPI NOR / SPI NAND 闪存驱动与分片校验刷写
- **SPIFC 控制器驱动**（[`failsafe/zx_flash.c`](failsafe/zx_flash.c)）：支持 SPI NOR 与 SPI NAND 自动探测，并在 Web 界面提供快速 Profile 切换（`128M NAND`、`64M NAND`、`16MB NOR`、`8MB NOR`）。
- **两阶段分片刷写与回读校验**（`/flash_step`）：
  - 第一阶段按 `1MB` 步进擦除目标区域（进度 `0% ~ 50%`）；
  - 第二阶段按 `256KB` 步进写入并执行 `memcmp` 回读校验（进度 `50% ~ 100%`），SPI NAND 写入支持坏块跳过与重定位；
  - 刷写完成后界面提示**请断电重启**（部分 PMIC 供电设备执行软复位后仅断开 USB 并进入 `wfi` 待机，需要重新插拔电源完成冷启动）。
- **备份与诊断**：支持整片或指定分区读取下载备份（`/backup`）以及 DRAM 内存测试（`/memtest`）。

### 4. 原厂分区表解析与引导支持
- 支持解析中兴动态分区表（`/get_partitions`），若未手动设置 `bootargs`，将根据检测到的分区布局自动生成默认启动参数（调试串口 `console=ttyS1,115200`，PL011 UART @ `0x01408000`）。
- 支持在 Web 界面读取与修改 `bootargs`（`/get_bootargs`、`/set_bootargs`），并提供常用预设模板。
- 内置原厂 `imagefs`（JFFS2）系统引导逻辑（[`failsafe/zx_stock_boot.c`](failsafe/zx_stock_boot.c)）。

### 5. LZMA 自解压打包
- 由于 ZX297520V3 阶段引导对 `2.uboot` 分区空间有限制（默认 160KB），[`compress/`](compress/) 目录提供了基于 LZMA 的 ARM 自解压引导与打包脚本 [`compress/package.py`](compress/package.py)，可将编译生成的 `u-boot.bin` 压缩封装为 `2.uboot`（160KB）与 `2.uboot_192k.bin`（192KB）。

---

## 目录结构（核心新增与修改）

```text
.
├── arch/arm/mach-zx/
│   ├── Kconfig
│   ├── Makefile
│   ├── cpu.c
│   └── init.c                  # DRAM 探测 (最大 128MB) 与硬件复位逻辑
├── board/zx/zx297520v3/
│   ├── Makefile
│   ├── gic_init.S
│   ├── zx297520v3.c            # 板级初始化、USB/Flash 启动分发、GPIO52 按键与 bootargs 生成
│   └── zx297520v3.env          # 默认环境变量
├── configs/
│   └── zx297520v3_defconfig    # 板级默认配置 (TEXT_BASE=0x21DF0000, LOAD_ADDR=0x25000000)
├── failsafe/
│   ├── Kconfig
│   ├── Makefile
│   ├── dhcpd.c / dhcpd.h       # 内置轻量级 DHCP 服务器
│   ├── failsafe.c              # Web HTTP 服务与刷写/备份/分区/bootargs 接口
│   ├── fs.c / fs.h             # 嵌入式静态文件查找
│   ├── zx_flash.c / zx_flash.h # SPI NOR / SPI NAND 控制器驱动与分片校验
│   ├── zx_stock_boot.c / .h    # 原厂 JFFS2 / imagefs 引导支持
│   └── fsdata/                 # Web 前端页面与静态资源 (index.html, flashing.html, main.js 等)
└── compress/
    ├── Makefile
    ├── package.py              # LZMA 自解压打包脚本，生成 uboot.uimg / 2.uboot / 2.uboot_192k.bin
    ├── head.S / decompress.c   # ARM 自解压引导代码
    └── LzmaDec.c / LzmaDec.h   # LZMA 解压器
```

---

## 编译与打包

### 1. 环境准备（以 Ubuntu 24.04 / WSL 为例）
需要安装 ARM 交叉编译工具链以及 U-Boot 构建、打包依赖：
```bash
sudo apt update
sudo apt install -y \
    gcc-arm-linux-gnueabihf \
    gcc-arm-linux-gnueabi \
    u-boot-tools \
    xz-utils \
    python3 \
    make bc bison flex libssl-dev device-tree-compiler
```

### 2. 编译 U-Boot
在仓库根目录执行：
```bash
make zx297520v3_defconfig
make -j$(nproc) CROSS_COMPILE=arm-linux-gnueabihf-
```
编译成功后会在根目录生成未压缩的 `u-boot.bin`。

### 3. 打包 LZMA 自解压镜像
进入 `compress/` 目录运行打包脚本（脚本内部会调用 `arm-linux-gnueabi-` 与 `mkimage`）：
```bash
cd compress
python3 package.py --uboot ../u-boot.bin
```
如需沿用原厂 `2.uboot` 的 384 字节签名头，可附加 `--header-from` 参数：
```bash
python3 package.py --uboot ../u-boot.bin --header-from /path/to/stock_2.uboot
```
打包完成后将在 `compress/` 下生成：
- `uboot.uimg`：带 uImage 头的自解压镜像
- `2.uboot`：160KB（163,840 字节）填充镜像，用于刷写标准 `uboot` 分区
- `2.uboot_192k.bin`：192KB（196,608 字节）镜像

---

## 使用说明

1. **进入 Web Failsafe**：通过 USB Boot 加载，或开机 3 秒内按住 Reset 键（GPIO 52）3 秒。
2. **连接设备**：电脑识别到 USB RNDIS 网卡后，内置 DHCP 会自动分配 `192.168.72.x` 地址；在浏览器打开 `http://192.168.72.1/`。
3. **核对闪存型号**：在页面中确认当前识别到的 SPI NOR / SPI NAND 容量与参数；若需手动指定，可使用页面上的快捷 Profile 按钮（`128M NAND` / `64M NAND` / `16MB NOR` / `8MB NOR`）。
4. **刷写与备份**：
   - 可先通过分区/整片备份功能导出原始闪存镜像；
   - 选择待刷写固件或自定义偏移上传后，系统会分片执行擦除、写入与回读校验；
   - 提示 **“闪存擦写与回读校验已顺利完成！✔ 请断电重启。”** 后，拔掉 USB/电源并重新上电启动。

## 致谢

本项目的代码部分除uboot原生代码外，部分来自于以下项目:

[zx297520v3-mainline/u-boot: Downstream U-Boot from 2011, kindly provided by ZTE support for the ZTE MF920U LTE router](https://github.com/zx297520v3-mainline/u-boot)

[zx297520v3-mainline/u-boot-mainline: "Das U-Boot" Source Tree](https://github.com/zx297520v3-mainline/u-boot-mainline)

[hanwckf/bl-mt798x: ATF and u-boot for mt798x.](https://github.com/hanwckf/bl-mt798x)

[Yuzhii0718/bl-mt798x-dhcpd at pr-sync-old-board-2025](https://github.com/Yuzhii0718/bl-mt798x-dhcpd/tree/pr-sync-old-board-2025?tab=readme-ov-file)

感谢Google Antigravity提供的Gemini 3.8 Flash与Gemini 4 Argon帮我实现了这个离谱的创意。

感谢[铸币](https://space.bilibili.com/35981144)与[小风时雨](https://github.com/yueluoying)两位朋友的测试与问题反馈。

以及感谢所有认真看完README并使用的各位用户......

## 不足之处

本来想做一个不死uboot，结果做了很久都没法顺利启动原厂系统，也做不出重启之类的，暂时就这样了。
