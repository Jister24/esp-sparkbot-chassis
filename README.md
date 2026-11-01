# ESP-SparkBot 履带底盘固件（ESP32-C2）

履带底盘主控的固件。在官方固件基础上加了一条**电量查询**指令，让 ESP-SparkBot 主体板
（ESP32-S3）通过 4P 磁吸口的 UART 问到底盘电池的电压、百分比和充电状态——上游固件其实
一直在采自己的电池，但只拿去控制 RGB 灯，数据不对外。

## 与上游的关系

| 项 | 值 |
|---|---|
| 上游 | `gitee.com/esp-friends/esp_sparkbot` → `example/tank/c2_tracked_chassis` |
| 基线 | `d42c5b4` |
| 许可证 | Apache-2.0（见 `LICENSE`，与上游一致） |
| 芯片 | ESP32-C2（ESP8684）rev2.0，26MHz 晶振，2MB flash |

本仓只含这一个 ESP-IDF 工程，`build/`、`managed_components/`、`sdkconfig` 不入库，
`dependencies.lock` 锁住三个受管组件（`led_indicator` / `led_strip` / `cmake_utilities`）。
上游的 3D 模型和 PCB 在 [立创开源](https://oshwhub.com/esp-college/esp-sparkbot)。

## 相对上游改了什么

| 文件 | 改动 |
|---|---|
| `components/power_management/include/power_management.h` | 新增 `battery_voltage_mv()` / `battery_percent()` / `battery_is_charging()` |
| `components/power_management/power_management.c` | 把采样结果存进静态快照（百分比夹到 0~100），对外提供读取函数 |
| `main/tracked_chassis.c` | 新增 `b` 命令；**去掉原来的原样回显**（主体板开始读 UART 后回显是纯噪声） |
| `sdkconfig.defaults` | 补上 `CONFIG_ESP_CONSOLE_UART_BAUDRATE=74880`，让新 clone 的构建能复现同样的控制台速率 |

## 硬件

由两个 N20 减速电机、ESP32-C2 主控板、前后 RGB 灯板、4P 磁吸连接器板、锂电池、
两个橡胶履带和 3D 打印结构组成。左右履带各由一个 N20 驱动，主动轮有牙槽防打滑。

4P 磁吸线序：`5V / GPIO48 / GPIO38 / GND`（标号是主体板 S3 侧的引脚）。实际数据走向：

```
主体板 S3 GPIO38 (TX)  ──→  C2 GPIO18 (UART1 RX)
主体板 S3 GPIO48 (RX)  ←──  C2 GPIO10 (UART1 TX)
```

### 底盘上的 USB 口是数据口

C2 芯片本身确实没有 USB 外设，但底盘 PCB 上**板载了一颗 USB-UART 桥（CH340，`0x1A86:0x7523`）**，
USB 口接的是这颗桥，桥的另一侧接 C2 的 **UART0（控制台，GPIO20/19）**。所以：

| 项 | 表现 |
|---|---|
| 枚举 | 插上就出现 `USB Serial`，macOS 26 **免驱**，端口 `/dev/cu.usbserial-XXXX` |
| 读日志 | 74880 8N1，能直接看到 C2 从 ROM 到 app 的完整启动过程 |
| 复位 | 打开串口会重启一次（DTR/RTS 接了 EN/GPIO9 的自动下载电路） |
| 烧写 | **能烧**，`esptool` 可直接进 ROM 下载模式，不需要飞线 |

也就是说**一根 USB 线就能看日志、能刷固件**。注意别把两根 USB 线搞混：

| 设备 | 端口形态 | 说明 |
|---|---|---|
| 履带底盘 | `usbserial-XXXX`（CH340 桥） | C2，控制台 74880 |
| 主体板 | `usbmodemXXXX`（ESP32-S3 原生 USB-Serial-JTAG，`0x303A:0x1001`） | S3，走 USB 直连 |

两个都插着时会同时出现，认芯片 VID 最稳：`0x1A86` 是底盘，`0x303A` 是主体板。

## 串口速查（最容易踩的坑）

底盘有**两个**独立串口，各管一摊：

| 口 | 引脚 | 波特率 | 用途 | 怎么接 |
|---|---|---|---|---|
| UART0 | GPIO20 (TX) / GPIO19 (RX) | **74880** | 控制台：只往外打日志，**不解析任何输入** | 板载 CH340 → 底盘 USB 口 |
| UART1 | GPIO10 (TX) / GPIO18 (RX) | 115200 | 主机指令口 | 4P 磁吸口 → 主体板 GPIO38/48 |

所以：**用 115200 读控制台必然是乱码**（实测可读率 31.9%，换 74880 就是 100%）；
而往控制台上发 `b0`、`x y` 没有反应也是正常的——解析逻辑在 UART1 上，不在 USB 那一侧。
USB 线只负责看日志和烧固件，发运动指令还得走磁吸口。

## 通信协议（UART1，115200 8N1）

`sscanf` 解析，命令之间**不带分隔符**，两条运动指令之间必须是**单个空格**。

| 指令 | 含义 | 说明 |
|---|---|---|
| `x<float> y<float>` | 运动 | 必须两个都在、单个空格分隔。底盘侧 `left = y+x`、`right = y-x`（×100 当占空比）。**y>0 前进，y<0 后退；x>0 右转，x<0 左转** |
| `d<float>` | 跳舞 | 值不参与逻辑，`d1` 是习惯写法。**阻塞约 17 秒**，期间不读串口 |
| `w<float>` | 灯光 | 0~8，切 WS2812 效果 |
| `c<float>` | 速度系数 | 标定用 |
| `b<float>` | 电量查询 | 回一行 `B:<毫伏>:<百分比>:<是否充电>`，值不参与逻辑 |

**看门狗**：循环里 `uart_read_count` 每轮 +1，超过 25（约 500ms）没有合法运动指令就
`spark_bot_motion_control(0, 0)` 自动停车。只有 `x y` 会清零计数，`b`/`w`/`d` 不喂狗——
所以一边遥控一边问电量不会互相干扰，但只发 `b0` 时车是停着的。

**回显**：上游会把收到的内容原样写回 UART1。本仓去掉了，因为主体板现在靠这条线读电量，
回显对收端全是噪声。

## 编译

```bash
. $IDF_PATH/export.sh          # ESP-IDF v5.5.x
idf.py build                   # sdkconfig.defaults 已指定 esp32c2 目标
```

产物 `build/tracked_chassis.bin` 约 232 KB（app 分区 0x100000，还剩 77%）。

## 烧写

**一根 USB 线，插底盘自己的 USB 口就行，不用飞线、不用拆壳。**

板载 CH340 的 DTR/RTS 已经接到 C2 的 `EN` / `GPIO9`，`esptool` 能自动把芯片拽进 ROM
下载模式——插上口直接烧：

```bash
# 先备份整片（2MB）
esptool.py -p /dev/cu.usbserial-XXXX -b 115200 read_flash 0 0x200000 chassis_backup.bin

# 只刷 app，不动 bootloader
idf.py -p /dev/cu.usbserial-XXXX -b 115200 app-flash
```

两个坑：**CH340 在 460800 下会丢字节**（`expected 0x1000 but received 0xffc`），读写都降到
115200，2MB 备份大约 190 秒；**控制台是 74880 不是 115200**，读日志要按 74880 开，
读不出来是乱码不是没起来。

不想装 ESP-IDF 时，也可以用 Release 里那份 `tracked_chassis.bin` 直接写 app 分区：

```bash
esptool.py -p /dev/cu.usbserial-XXXX -b 115200 write_flash 0x10000 tracked_chassis.bin
```

> 早期文档写过「USB 口只供电、必须飞线测试点」，那是错的。实测 USB 口就是数据口，
> 上面整套流程都是从这个口跑通的。

## 验证

刷完保持 USB 连着，按 **74880** 打开控制台，主体板连着的话每 5 秒会出现一行：

```
I (5420)  Tracked-Chassis: Command: b, Value: 0.00
I (10430) Tracked-Chassis: Command: b, Value: 0.00
```

这是主体板 `ChassisBattery` 在轮询，出现即说明 UART1 链路双向通、补丁生效。再确认 App
的电源卡里底盘那张有百分比，就完全通了。

刷完拔掉 USB 前确认一下 `rst:0x1 (POWERON)` 之后没有 `Guru Meditation` / 反复重启，就没问题。

## 相关仓库

- 主体板（ESP32-S3）定制固件：[Jister24/xiaozhi-esp32-sparkbot](https://github.com/Jister24/xiaozhi-esp32-sparkbot)
- 上层应用（Android App / 局域网客户端 / 文档）：[Jister24/ESP_SPARKBOT](https://github.com/Jister24/ESP_SPARKBOT)
