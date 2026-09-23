/*
 * touch_cst816d.cpp —— CST816D I2C 采集（Arduino Wire）
 *
 * 寄存器（CST816x 系列标准）：
 *   0x01 GESTURE  手势码
 *   0x02 FINGER   触点数（0 = 抬起）
 *   0x03 XH / 0x04 XL   X 坐标（12bit）
 *   0x05 YH / 0x06 YL   Y 坐标（12bit）
 * 从 0x01 开始连续读 6 个字节就能拿全。
 */
#include "touch_cst816d.h"

#include <Arduino.h>
#include <Wire.h>

#define REG_GESTURE      0x01
#define REG_FINGER       0x02
#define REG_DISAUTOSLEEP 0xFE    /* 写 0x01 = 关闭自动休眠（可选，见下） */

/* CST816D 空闲一段时间会自己进低功耗，可能表现为"过一会儿点不动"。
 * 写 0xFE=1 可关掉自动休眠；写失败也不影响读点（不同批次寄存器可能不同）。 */
#ifndef TOUCH_DISABLE_AUTO_SLEEP
#define TOUCH_DISABLE_AUTO_SLEEP 1
#endif

static bool s_present = false;

/* ---------------- 底层读寄存器 ---------------- */
static bool cst_read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    Wire.beginTransmission((uint8_t)TOUCH_I2C_ADDR);
    Wire.write(reg);
    /* false = 不发 STOP，紧接着 repeated start 去读（标准 I2C 读寄存器时序） */
    if (Wire.endTransmission(false) != 0) {
        return false;                       /* 没 ACK：芯片不在/总线被拉死 */
    }
    /* 用 (int,int,bool) 重载：ESP32 core 2.x/3.x 的 Wire 都有它；
     * 若你的 core 报重载不匹配，改成 (uint16_t)TOUCH_I2C_ADDR, (uint8_t)len, (bool)true 即可 */
    if (Wire.requestFrom((int)TOUCH_I2C_ADDR, (int)len, true) != len) {
        return false;
    }
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)Wire.read();
    }
    return true;
}

static bool cst_write_reg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission((uint8_t)TOUCH_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

/* ---------------- 初始化 ---------------- */
void touch_drv_init(void)
{
    /* 复位：低 10ms → 高 100ms（与官方例程一致） */
    pinMode(TOUCH_RST_PIN, OUTPUT);
    digitalWrite(TOUCH_RST_PIN, LOW);
    delay(10);
    digitalWrite(TOUCH_RST_PIN, HIGH);
    delay(100);

    /* INT：我们做轮询就够了，只把它配成上拉输入（别在中断里做 I2C） */
    pinMode(TOUCH_INT_PIN, INPUT_PULLUP);

    /* I2C 400kHz（注意：15/14 上与板载电源/IMU/RTC 共用，不要再加别的初始化） */
    Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL, TOUCH_I2C_FREQ);

    /* 探测：能 ACK 就说明芯片在线 */
    Wire.beginTransmission((uint8_t)TOUCH_I2C_ADDR);
    s_present = (Wire.endTransmission() == 0);

#if TOUCH_DISABLE_AUTO_SLEEP
    if (s_present) {
        (void)cst_write_reg(REG_DISAUTOSLEEP, 0x01);
    }
#endif

    Serial.printf("[touch] CST816D %s (addr 0x%02X, SDA=%d, SCL=%d)\n",
                  s_present ? "OK" : "NOT FOUND",
                  TOUCH_I2C_ADDR, TOUCH_I2C_SDA, TOUCH_I2C_SCL);
}

bool touch_drv_present(void)
{
    return s_present;
}

/* ---------------- 读一次触点 ---------------- */
bool touch_drv_read(touch_state_t *st)
{
    uint8_t d[6];

    if (st == NULL) {
        return false;
    }

    if (!cst_read_regs(REG_GESTURE, d, (uint8_t)sizeof(d))) {
        return false;                        /* I2C 出错：本次不产生任何事件 */
    }

    st->gesture = d[0];
    st->fingers = (uint8_t)(d[1] & 0x0F);
    st->x = (uint16_t)(((uint16_t)(d[2] & 0x0F) << 8) | d[3]);   /* 12bit */
    st->y = (uint16_t)(((uint16_t)(d[4] & 0x0F) << 8) | d[5]);   /* 12bit */

    /* 方向镜像（默认关）：只有发现"手指与读数方向相反"时才在头文件里打开 */
#if TOUCH_MIRROR_X
    st->x = (uint16_t)((TOUCH_PANEL_W - 1) - st->x);
#endif
#if TOUCH_MIRROR_Y
    st->y = (uint16_t)((TOUCH_PANEL_H - 1) - st->y);
#endif

    /* fingers==0 时 x/y 可能是锁存的最后位置（抬手瞬间还能用来算"甩动"），
     * 上层只在 fingers>0 时把它当"当前触点"用，抬手判定见 §1.8。 */
    return true;
}