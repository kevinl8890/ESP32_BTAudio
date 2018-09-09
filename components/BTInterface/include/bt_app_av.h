/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef __BT_APP_AV_H__
#define __BT_APP_AV_H__

#include <stdint.h>
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "types.hpp"

#include "I2SOutput.hpp"

namespace DSP {
  class SignalChain;
}

#define BT_AV_TAG               "BT_AV"

void set_signalChain(DSP::SignalChain* left, DSP::SignalChain* right);

void set_stereo_mode(Audio::StereoMode stereoMode);

void set_i2s_output(I2SInterface::I2SOutput* i2sOutput);

/**
 * @brief     callback function for A2DP sink
 */
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

/**
 * @brief     callback function for A2DP sink audio data stream
 */
void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len);

/**
 * @brief     callback function for AVRCP controller
 */
void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

#endif /* __BT_APP_AV_H__*/