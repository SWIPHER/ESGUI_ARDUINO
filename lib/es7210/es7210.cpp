/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Arduino adaptation:
 *   I2C access uses Arduino Wire.
 *
 * Target:
 *   ESP32-S3
 *   Arduino-ESP32 Core 3.x
 */

#include <Arduino.h>
#include <inttypes.h>
#include <stdlib.h>
#include <Wire.h>
#include "es7210.h"
#include "es7210_reg.h"

/*
 * IMPORTANT:
 *
 * Do NOT include:
 *
 *   driver/i2c.h
 *   esp_driver_i2c.h
 *
 * This Arduino adaptation uses Wire exclusively.
 *
 * This avoids the ESP32 Arduino Core 3.x conflict:
 *
 *   "driver_ng is not allowed to be used with this old driver"
 */

// ============================================================
// Validation macros
// ============================================================

#define IS_ES7210_I2S_FMT(val) \
    (((val) == ES7210_I2S_FMT_I2S) || \
     ((val) == ES7210_I2S_FMT_LJ) || \
     ((val) == ES7210_I2S_FMT_DSP_A) || \
     ((val) == ES7210_I2S_FMT_DSP_B))

#define IS_ES7210_I2S_BITS(val) \
    (((val) == ES7210_I2S_BITS_24B) || \
     ((val) == ES7210_I2S_BITS_20B) || \
     ((val) == ES7210_I2S_BITS_18B) || \
     ((val) == ES7210_I2S_BITS_16B) || \
     ((val) == ES7210_I2S_BITS_32B))

#define IS_ES7210_MIC_GAIN(val) \
    (((val) >= ES7210_MIC_GAIN_0DB) && \
     ((val) <= ES7210_MIC_GAIN_37_5DB))

#define IS_ES7210_MIC_BIAS(val) \
    (((val) == ES7210_MIC_BIAS_2V18) || \
     ((val) == ES7210_MIC_BIAS_2V26) || \
     ((val) == ES7210_MIC_BIAS_2V36) || \
     ((val) == ES7210_MIC_BIAS_2V45) || \
     ((val) == ES7210_MIC_BIAS_2V55) || \
     ((val) == ES7210_MIC_BIAS_2V66) || \
     ((val) == ES7210_MIC_BIAS_2V78) || \
     ((val) == ES7210_MIC_BIAS_2V87))

// ============================================================
// Internal device structure
// ============================================================

struct es7210_dev_t
{
    /*
     * Kept for compatibility with the original API.
     *
     * The Arduino implementation does NOT use this field
     * to access I2C. All I2C traffic goes through Wire.
     */
    i2c_port_t i2c_port;

    uint8_t i2c_addr;
};

// ============================================================
// Clock coefficient structure
// ============================================================

typedef struct
{
    uint32_t mclk;
    uint32_t lrck;
    uint8_t ss_ds;
    uint8_t adc_div;
    uint8_t dll;
    uint8_t doubler;
    uint8_t osr;
    uint8_t mclk_src;
    uint32_t lrck_h;
    uint32_t lrck_l;
} coeff_div_t;

// ============================================================
// Clock coefficient table
// ============================================================

static const coeff_div_t es7210_coeff_div[] =
{
    /* 8k */
    {12288000,  8000,  0x00, 0x03, 0x01, 0x00, 0x20, 0x00, 0x06, 0x00},
    {16384000,  8000,  0x00, 0x04, 0x01, 0x00, 0x20, 0x00, 0x08, 0x00},
    {19200000,  8000,  0x00, 0x1e, 0x00, 0x01, 0x28, 0x00, 0x09, 0x60},
    {4096000,   8000,  0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},

    /* 11.025k */
    {11289600,  11025, 0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x01, 0x00},

    /* 12k */
    {12288000,  12000, 0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x04, 0x00},
    {19200000,  12000, 0x00, 0x14, 0x00, 0x01, 0x28, 0x00, 0x06, 0x40},

    /* 16k */
    {4096000,   16000, 0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},
    {19200000,  16000, 0x00, 0x0a, 0x00, 0x00, 0x1e, 0x00, 0x04, 0x80},
    {16384000,  16000, 0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x04, 0x00},
    {12288000,  16000, 0x00, 0x03, 0x01, 0x01, 0x20, 0x00, 0x03, 0x00},

    /* 22.05k */
    {11289600,  22050, 0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},

    /* 24k */
    {12288000,  24000, 0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},
    {19200000,  24000, 0x00, 0x0a, 0x00, 0x01, 0x28, 0x00, 0x03, 0x20},

    /* 32k */
    {12288000,  32000, 0x00, 0x03, 0x00, 0x00, 0x20, 0x00, 0x01, 0x80},
    {16384000,  32000, 0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},
    {19200000,  32000, 0x00, 0x05, 0x00, 0x00, 0x1e, 0x00, 0x02, 0x58},

    /* 44.1k */
    {11289600,  44100, 0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},

    /* 48k */
    {12288000,  48000, 0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},
    {19200000,  48000, 0x00, 0x05, 0x00, 0x01, 0x28, 0x00, 0x01, 0x90},

    /* 64k */
    {16384000,  64000, 0x01, 0x01, 0x01, 0x00, 0x20, 0x00, 0x01, 0x00},
    {19200000,  64000, 0x00, 0x05, 0x00, 0x01, 0x1e, 0x00, 0x01, 0x2c},

    /* 88.2k */
    {11289600,  88200, 0x01, 0x01, 0x01, 0x01, 0x20, 0x00, 0x00, 0x80},

    /* 96k */
    {12288000,  96000, 0x01, 0x01, 0x01, 0x01, 0x20, 0x00, 0x00, 0x80},
    {19200000,  96000, 0x01, 0x05, 0x00, 0x01, 0x28, 0x00, 0x00, 0xC8},
};

// ============================================================
// Find clock coefficient
// ============================================================

static const coeff_div_t* es7210_get_coeff(
    uint32_t mclk,
    uint32_t lrck
)
{
    const size_t count =
        sizeof(es7210_coeff_div) /
        sizeof(es7210_coeff_div[0]);

    for (size_t i = 0; i < count; ++i)
    {
        if (
            es7210_coeff_div[i].lrck == lrck &&
            es7210_coeff_div[i].mclk == mclk
        )
        {
            return &es7210_coeff_div[i];
        }
    }

    return NULL;
}

// ============================================================
// Arduino Wire register write
// ============================================================

static esp_err_t es7210_write_reg(
    es7210_dev_handle_t handle,
    uint8_t reg_addr,
    uint8_t reg_val
)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    Wire.beginTransmission(
        handle->i2c_addr
    );

    Wire.write(reg_addr);
    Wire.write(reg_val);

    const uint8_t result =
        Wire.endTransmission(true);

    if (result != 0)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

// ============================================================
// Arduino Wire register read
// ============================================================

static esp_err_t es7210_read_reg(
    es7210_dev_handle_t handle,
    uint8_t reg_addr,
    uint8_t* reg_val
)
{
    if (
        handle == NULL ||
        reg_val == NULL
    )
    {
        return ESP_ERR_INVALID_ARG;
    }

    Wire.beginTransmission(
        handle->i2c_addr
    );

    Wire.write(reg_addr);

    /*
     * Repeated-start.
     */
    const uint8_t result =
        Wire.endTransmission(false);

    if (result != 0)
    {
        return ESP_FAIL;
    }

    const size_t received =
        Wire.requestFrom(
            static_cast<int>(
                handle->i2c_addr
            ),
            1,
            true
        );

    if (received != 1)
    {
        return ESP_FAIL;
    }

    *reg_val =
        Wire.read();

    return ESP_OK;
}

// ============================================================
// Write register helper macro
// ============================================================

#define ES7210_WRITE_REG(reg_addr, reg_value)       \
    do {                                             \
        esp_err_t _ret = es7210_write_reg(          \
            handle,                                  \
            (reg_addr),                              \
            (reg_value)                              \
        );                                           \
        if (_ret != ESP_OK) {                        \
            return _ret;                             \
        }                                            \
    } while (0)

// ============================================================
// Set I2S format
// ============================================================

static esp_err_t es7210_set_i2s_format(
    es7210_dev_handle_t handle,
    es7210_i2s_fmt_t i2s_format,
    es7210_i2s_bits_t bit_width,
    bool tdm_enable
)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!IS_ES7210_I2S_FMT(i2s_format))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!IS_ES7210_I2S_BITS(bit_width))
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_val = 0;

    // --------------------------------------------------------
    // Bit width
    // --------------------------------------------------------

    switch (bit_width)
    {
        case ES7210_I2S_BITS_16B:
            reg_val = 0x60;
            break;

        case ES7210_I2S_BITS_18B:
            reg_val = 0x40;
            break;

        case ES7210_I2S_BITS_20B:
            reg_val = 0x20;
            break;

        case ES7210_I2S_BITS_24B:
            reg_val = 0x00;
            break;

        case ES7210_I2S_BITS_32B:
            reg_val = 0x80;
            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    ES7210_WRITE_REG(
        ES7210_SDP_INTERFACE1_REG11,
        i2s_format | reg_val
    );

    // --------------------------------------------------------
    // Protocol / TDM
    // --------------------------------------------------------

    switch (i2s_format)
    {
        case ES7210_I2S_FMT_I2S:
            reg_val = 0x02;
            break;

        case ES7210_I2S_FMT_LJ:
            reg_val = 0x02;
            break;

        case ES7210_I2S_FMT_DSP_A:
            reg_val = 0x01;
            break;

        case ES7210_I2S_FMT_DSP_B:
            reg_val = 0x01;
            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    if (tdm_enable)
    {
        ES7210_WRITE_REG(
            ES7210_SDP_INTERFACE2_REG12,
            reg_val
        );
    }
    else
    {
        ES7210_WRITE_REG(
            ES7210_SDP_INTERFACE2_REG12,
            0x00
        );
    }

    return ESP_OK;
}

// ============================================================
// Set sample rate
// ============================================================

static esp_err_t es7210_set_i2s_sample_rate(
    es7210_dev_handle_t handle,
    uint32_t sample_rate_hz,
    uint32_t mclk_ratio
)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t mclk_freq_hz =
        sample_rate_hz * mclk_ratio;

    const coeff_div_t* coeff =
        es7210_get_coeff(
            mclk_freq_hz,
            sample_rate_hz
        );

    if (coeff == NULL)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    // OSR
    ES7210_WRITE_REG(
        ES7210_OSR_REG07,
        coeff->osr
    );

    // ADC divider + doubler + DLL
    ES7210_WRITE_REG(
        ES7210_MAINCLK_REG02,
        (coeff->adc_div) |
        (coeff->doubler << 6) |
        (coeff->dll << 7)
    );

    // LRCK
    ES7210_WRITE_REG(
        ES7210_LRCK_DIVH_REG04,
        coeff->lrck_h
    );

    ES7210_WRITE_REG(
        ES7210_LRCK_DIVL_REG05,
        coeff->lrck_l
    );

    return ESP_OK;
}

// ============================================================
// Set microphone gain
// ============================================================

static esp_err_t es7210_set_mic_gain(
    es7210_dev_handle_t handle,
    es7210_mic_gain_t mic_gain
)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!IS_ES7210_MIC_GAIN(mic_gain))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ES7210_WRITE_REG(
        ES7210_MIC1_GAIN_REG43,
        mic_gain | 0x10
    );

    ES7210_WRITE_REG(
        ES7210_MIC2_GAIN_REG44,
        mic_gain | 0x10
    );

    ES7210_WRITE_REG(
        ES7210_MIC3_GAIN_REG45,
        mic_gain | 0x10
    );

    ES7210_WRITE_REG(
        ES7210_MIC4_GAIN_REG46,
        mic_gain | 0x10
    );

    return ESP_OK;
}

// ============================================================
// Set microphone bias
// ============================================================

static esp_err_t es7210_set_mic_bias(
    es7210_dev_handle_t handle,
    es7210_mic_bias_t mic_bias
)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!IS_ES7210_MIC_BIAS(mic_bias))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ES7210_WRITE_REG(
        ES7210_MIC12_BIAS_REG41,
        mic_bias
    );

    ES7210_WRITE_REG(
        ES7210_MIC34_BIAS_REG42,
        mic_bias
    );

    return ESP_OK;
}

// ============================================================
// Create codec
// ============================================================

esp_err_t es7210_new_codec(
    const es7210_i2c_config_t* i2c_conf,
    es7210_dev_handle_t* handle_out
)
{
    if (
        i2c_conf == NULL ||
        handle_out == NULL
    )
    {
        return ESP_ERR_INVALID_ARG;
    }

    struct es7210_dev_t* handle =
        static_cast<struct es7210_dev_t*>(
            calloc(
                1,
                sizeof(struct es7210_dev_t)
            )
        );

    if (handle == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    handle->i2c_port =
        i2c_conf->i2c_port;

    handle->i2c_addr =
        i2c_conf->i2c_addr;

    *handle_out =
        handle;

    return ESP_OK;
}

// ============================================================
// Delete codec
// ============================================================

esp_err_t es7210_del_codec(
    es7210_dev_handle_t handle
)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    free(handle);

    return ESP_OK;
}

// ============================================================
// Configure codec
// ============================================================

esp_err_t es7210_config_codec(
    es7210_dev_handle_t handle,
    const es7210_codec_config_t* codec_conf
)
{
    if (
        handle == NULL ||
        codec_conf == NULL
    )
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    // --------------------------------------------------------
    // Software reset
    // --------------------------------------------------------

    ES7210_WRITE_REG(
        ES7210_RESET_REG00,
        0xFF
    );

    ES7210_WRITE_REG(
        ES7210_RESET_REG00,
        0x32
    );

    // --------------------------------------------------------
    // Initialization timing
    // --------------------------------------------------------

    ES7210_WRITE_REG(
        ES7210_TIME_CONTROL0_REG09,
        0x30
    );

    ES7210_WRITE_REG(
        ES7210_TIME_CONTROL1_REG0A,
        0x30
    );

    // --------------------------------------------------------
    // ADC HPF
    // --------------------------------------------------------

    ES7210_WRITE_REG(
        ES7210_ADC12_HPF1_REG23,
        0x2A
    );

    ES7210_WRITE_REG(
        ES7210_ADC12_HPF2_REG22,
        0x0A
    );

    ES7210_WRITE_REG(
        ES7210_ADC34_HPF1_REG21,
        0x2A
    );

    ES7210_WRITE_REG(
        ES7210_ADC34_HPF2_REG20,
        0x0A
    );

    // --------------------------------------------------------
    // I2S / TDM
    // --------------------------------------------------------

    ret =
        es7210_set_i2s_format(
            handle,
            codec_conf->i2s_format,
            codec_conf->bit_width,
            codec_conf->flags.tdm_enable
        );

    if (ret != ESP_OK)
    {
        return ret;
    }

    // --------------------------------------------------------
    // Analog power
    // --------------------------------------------------------

    ES7210_WRITE_REG(
        ES7210_ANALOG_REG40,
        0xC3
    );

    // --------------------------------------------------------
    // MIC bias
    // --------------------------------------------------------

    ret =
        es7210_set_mic_bias(
            handle,
            codec_conf->mic_bias
        );

    if (ret != ESP_OK)
    {
        return ret;
    }

    // --------------------------------------------------------
    // MIC gain
    // --------------------------------------------------------

    ret =
        es7210_set_mic_gain(
            handle,
            codec_conf->mic_gain
        );

    if (ret != ESP_OK)
    {
        return ret;
    }

    // --------------------------------------------------------
    // Power MIC1-4
    // --------------------------------------------------------

    ES7210_WRITE_REG(
        ES7210_MIC1_POWER_REG47,
        0x08
    );

    ES7210_WRITE_REG(
        ES7210_MIC2_POWER_REG48,
        0x08
    );

    ES7210_WRITE_REG(
        ES7210_MIC3_POWER_REG49,
        0x08
    );

    ES7210_WRITE_REG(
        ES7210_MIC4_POWER_REG4A,
        0x08
    );

    // --------------------------------------------------------
    // Sample rate
    // --------------------------------------------------------

    ret =
        es7210_set_i2s_sample_rate(
            handle,
            codec_conf->sample_rate_hz,
            codec_conf->mclk_ratio
        );

    if (ret != ESP_OK)
    {
        return ret;
    }

    // --------------------------------------------------------
    // Power down DLL
    // --------------------------------------------------------

    ES7210_WRITE_REG(
        ES7210_POWER_DOWN_REG06,
        0x04
    );

    // --------------------------------------------------------
    // Power on MIC1-4 / ADC / PGA
    // --------------------------------------------------------

    ES7210_WRITE_REG(
        ES7210_MIC12_POWER_REG4B,
        0x0F
    );

    ES7210_WRITE_REG(
        ES7210_MIC34_POWER_REG4C,
        0x0F
    );

    // --------------------------------------------------------
    // Enable device
    // --------------------------------------------------------

    ES7210_WRITE_REG(
        ES7210_RESET_REG00,
        0x71
    );

    ES7210_WRITE_REG(
        ES7210_RESET_REG00,
        0x41
    );

    return ESP_OK;
}

// ============================================================
// ADC digital volume
// ============================================================

esp_err_t es7210_config_volume(
    es7210_dev_handle_t handle,
    int8_t volume_db
)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (
        volume_db < -95 ||
        volume_db > 32
    )
    {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * 0x00   = -95.5 dB
     * 0xBF   = 0 dB
     * 0xFF   = +32 dB
     *
     * step = 0.5 dB
     */

    const uint8_t reg_val =
        static_cast<uint8_t>(
            191 + volume_db * 2
        );

    ES7210_WRITE_REG(
        ES7210_ADC1_DIRECT_DB_REG1B,
        reg_val
    );

    ES7210_WRITE_REG(
        ES7210_ADC2_DIRECT_DB_REG1C,
        reg_val
    );

    ES7210_WRITE_REG(
        ES7210_ADC3_DIRECT_DB_REG1D,
        reg_val
    );

    ES7210_WRITE_REG(
        ES7210_ADC4_DIRECT_DB_REG1E,
        reg_val
    );

    return ESP_OK;
}

// ============================================================
// Reset
// ============================================================

esp_err_t es7210_reset(
    es7210_dev_handle_t handle
)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    ret =
        es7210_write_reg(
            handle,
            ES7210_RESET_REG00,
            0xFF
        );

    if (ret != ESP_OK)
    {
        return ret;
    }

    delay(10);

    ret =
        es7210_write_reg(
            handle,
            ES7210_RESET_REG00,
            0x32
        );

    return ret;
}