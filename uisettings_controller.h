#pragma once

#include "encoder_info.h"
#include "wrapper/plugin_api.h"

namespace IOPlugin {

class UISettingsController final {
   public:
    explicit UISettingsController(const EncoderInfo& encoderInfo);
    UISettingsController(const HostCodecConfigCommon& commonProps, const EncoderInfo& encoderInfo);
    void Load(IPropertyProvider* values);
    StatusCode Render(HostListRef* settingsList) const;

   private:
    void InitDefaults();
    void SetFirstSupportedQualityMode();
    StatusCode RenderQuality(HostListRef* settingsList) const;

   public:
    QualityMode GetQualityMode() const;
    int32_t GetQP() const;
    int32_t GetBitRate() const;
    int32_t GetPreset() const;
    const std::string& GetCustomParams() const;

   private:
    HostCodecConfigCommon commonProps;
    EncoderInfo encoderInfo;

    QualityMode qualityMode{};
    int32_t qp{};
    int32_t bitRate{};
    int32_t preset{};
    std::string customParams;

    std::string qualityModeId;
    std::string qpId;
    std::string bitrateId;
    std::string presetId;
    std::string customParamsId;
};

}
