#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void set_power_current_light_mode(light_mode_t mode);
void battery_voltage_monitor_start(void);

// 电量对外查询。原本这套采样只喂给 RGB 灯，值烂在 vbat_monitor_task 的局部变量里；
// 主体板要通过 UART 读底盘电量，得把它提出来。
// 三者都读的是最近一次采样结果，未采样过时返回 0 / false。
int battery_voltage_mv(void);
int battery_percent(void);
bool battery_is_charging(void);

#ifdef __cplusplus
}
#endif
