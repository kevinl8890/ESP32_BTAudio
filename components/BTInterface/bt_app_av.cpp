
/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "esp_log.h"
#include "bt_app_core.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
}

#include "bt_app_av.h"

#include "SignalChain.hpp"
#include "I2SOutput.hpp"


static constexpr int BUFFER_SIZE = 1024;

static DSP::SignalChain *m_signalChainLeft, *m_signalChainRight;
static Audio::StereoMode m_stereoMode{Audio::StereoMode::Stereo};

static I2SInterface::I2SOutput *m_i2sOutput;

static Audio::SampleBuffer m_leftSamples(BUFFER_SIZE),
    m_rightSamples(BUFFER_SIZE);
static std::vector<uint8_t> m_audioBuffer(BUFFER_SIZE*4);

/* a2dp event handler */
static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param);
/* avrc event handler */
static void bt_av_hdl_avrc_evt(uint16_t event, void *p_param);

static uint32_t m_pkt_cnt = 0;
static esp_a2d_audio_state_t m_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
static const char *m_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
static const char *m_a2d_audio_state_str[] = {"Suspended", "Stopped", "Started"};

void set_signalChain(DSP::SignalChain* signalChainLeft,
  DSP::SignalChain* signalChainRight) {
  
  m_signalChainLeft = signalChainLeft;
  m_signalChainRight = signalChainRight;
}

void set_stereo_mode(Audio::StereoMode stereoMode) {
  m_stereoMode = stereoMode;
}

void set_i2s_output(I2SInterface::I2SOutput* i2sOutput) {
    m_i2sOutput = i2sOutput;
}

/* callback for A2DP sink */
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT: {
        bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param, sizeof(esp_a2d_cb_param_t), NULL);
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "Invalid A2DP event: %d", event);
        break;
    }
}

void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    int sampleCount = len / 4;
    m_leftSamples.resize(sampleCount);
    m_rightSamples.resize(sampleCount);

    for(int i = 0; i < sampleCount; ++i) {
        int offset = 4*i;

        std::memcpy(&m_rightSamples[i], data + offset, 2);
        std::memcpy(&m_leftSamples[i], data + offset + 2, 2);
    }
    
    m_signalChainLeft->processSamples(m_leftSamples);
    m_signalChainRight->processSamples(m_rightSamples);

    m_i2sOutput->writeSamples(m_leftSamples, m_rightSamples);

    if (++m_pkt_cnt % 500 == 0) {
        ESP_LOGI(BT_AV_TAG, "Audio packet count %u, packet size %d bytes", m_pkt_cnt, len);
    }
}

void bt_app_alloc_meta_buffer(esp_avrc_ct_cb_param_t *param)
{
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(param);
    uint8_t *attr_text = (uint8_t *) malloc (rc->meta_rsp.attr_length + 1);
    memcpy(attr_text, rc->meta_rsp.attr_text, rc->meta_rsp.attr_length);
    attr_text[rc->meta_rsp.attr_length] = 0;

    rc->meta_rsp.attr_text = attr_text;
}

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_METADATA_RSP_EVT:
        bt_app_alloc_meta_buffer(param);
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
        bt_app_work_dispatch(bt_av_hdl_avrc_evt, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s evt %d", __func__, event);
    esp_a2d_cb_param_t *a2d = NULL;
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        uint8_t *bda = a2d->conn_stat.remote_bda;
        ESP_LOGI(BT_AV_TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
             m_a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_AV_TAG, "A2DP audio state: %s", m_a2d_audio_state_str[a2d->audio_stat.state]);
        m_audio_state = a2d->audio_stat.state;
        if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
            m_pkt_cnt = 0;
        }
        break;
    }
    case ESP_A2D_AUDIO_CFG_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_AV_TAG, "A2DP audio stream configuration, codec type %d", a2d->audio_cfg.mcc.type);
        // for now only SBC stream is supported
        if (a2d->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            int sample_rate = 16000;
            char oct0 = a2d->audio_cfg.mcc.cie.sbc[0];
            if (oct0 & (0x01 << 6)) {
                sample_rate = 32000;
            } else if (oct0 & (0x01 << 5)) {
                sample_rate = 44100;
            } else if (oct0 & (0x01 << 4)) {
                sample_rate = 48000;
            }
            i2s_set_clk(I2S_NUM_0, sample_rate, I2S_BITS_PER_SAMPLE_16BIT,
              I2S_CHANNEL_STEREO);
            m_signalChainLeft->setSampleRate(sample_rate);
            m_signalChainRight->setSampleRate(sample_rate);

            ESP_LOGI(BT_AV_TAG, "Configure audio player %x-%x-%x-%x",
                     a2d->audio_cfg.mcc.cie.sbc[0],
                     a2d->audio_cfg.mcc.cie.sbc[1],
                     a2d->audio_cfg.mcc.cie.sbc[2],
                     a2d->audio_cfg.mcc.cie.sbc[3]);
            ESP_LOGI(BT_AV_TAG, "Audio player configured, sample rate=%d", sample_rate);
        }
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}

static void bt_av_new_track()
{
    //Register notifications and request metadata
    esp_avrc_ct_send_metadata_cmd(0, ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_GENRE);
    esp_avrc_ct_send_register_notification_cmd(1, ESP_AVRC_RN_TRACK_CHANGE, 0);
}

void bt_av_notify_evt_handler(uint8_t event_id, uint32_t event_parameter)
{
    switch (event_id) {
    case ESP_AVRC_RN_TRACK_CHANGE:
        bt_av_new_track();
        break;
    }
}

static void bt_av_hdl_avrc_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s evt %d", __func__, event);
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(p_param);
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        uint8_t *bda = rc->conn_stat.remote_bda;
        ESP_LOGI(BT_AV_TAG, "AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

        if (rc->conn_stat.connected) {
            bt_av_new_track();
        }
        break;
    }
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
        ESP_LOGI(BT_AV_TAG, "AVRC passthrough rsp: key_code 0x%x, key_state %d", rc->psth_rsp.key_code, rc->psth_rsp.key_state);
        break;
    }
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        ESP_LOGI(BT_AV_TAG, "AVRC metadata rsp: attribute id 0x%x, %s", rc->meta_rsp.attr_id, rc->meta_rsp.attr_text);
        free(rc->meta_rsp.attr_text);
        break;
    }
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        ESP_LOGI(BT_AV_TAG, "AVRC event notification: %d, param: %d", rc->change_ntf.event_id, rc->change_ntf.event_parameter);
        bt_av_notify_evt_handler(rc->change_ntf.event_id, rc->change_ntf.event_parameter);
        break;
    }
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
        ESP_LOGI(BT_AV_TAG, "AVRC remote features %x", rc->rmt_feats.feat_mask);
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}