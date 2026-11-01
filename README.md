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

**底盘上那个 USB 口只接了供电。** ESP32-C2 芯片本身没有 USB 外设，插上电脑不会有任何
设备枚举出来——这不是驱动问题，换什么线都一样。

## 串口速查（最容易踩的坑）

底盘有**两个**独立串口，各管一摊：

| 口 | 引脚 | 波特率 | 用途 |
|---|---|---|---|
| UART0 | GPIO20 (TX) / GPIO19 (RX) | **74880** | 控制台，只往外打日志，**不解析任何输入** |
| UART1 | GPIO10 (TX) / GPIO18 (RX) | 115200 | 主机指令口，就是 4P 磁吸口 |

所以：**用 115200 读控制台必然是乱码**（实测可读率 31.9%，换 74880 就是 100%）；
而往控制台上发 `b0`、`x y` 没有反应也是正常的——解析逻辑在 UART1 上。

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

C2 的 `EN` 和 `GPIO9`（BOOT）都引到了测试点，必须飞线，光接 TX/RX 是进不去下载模式的：

```
3V3 / GND / U0TXD(GPIO20) / U0RXD(GPIO19) / EN / GPIO9
```

USB-TTL 用 CH340 或 CP2102 都行，3.3V 电平，**共地一定要接**。

```bash
# 先备份整片（2MB）
esptool.py -p /dev/cu.usbserial-XXXX -b 115200 read_flash 0 0x200000 chassis_backup.bin

# 只刷 app，不动 bootloader
idf.py -p /dev/cu.usbserial-XXXX -b 115200 app-flash
```

**CH340 在 460800 下会丢字节**（`expected 0x1000 but received 0xffc`），读写都降到 115200，
2MB 备份大约 190 秒。

## 验证

刷完打开控制台（74880），主体板连着的话每 5 秒会出现一行：

```
I (5420)  Tracked-Chassis: Command: b, Value: 0.00
I (10430) Tracked-Chassis: Command: b, Value: 0.00
```

这是主体板 `ChassisBattery` 在轮询，出现即说明链路双向通、补丁生效。再确认 App 的电源卡
里底盘那张有百分比，就完全通了。

## 相关仓库

- 主体板（ESP32-S3）定制固件：[Jister24/xiaozhi-esp32-sparkbot](https://github.com/Jister24/xiaozhi-esp32-sparkbot)
- 上层应用（Android App / 局域网客户端 / 文档）：[Jister24/ESP_SPARKBOT](https://github.com/Jister24/ESP_SPARKBOT)
