#include "ffmpeg_encoder.h"

UISettingsController::UISettingsController(const EncoderInfo& encoderInfo) : encoderInfo(encoderInfo) {
    InitDefaults();
}

UISettingsController::UISettingsController(const HostCodecConfigCommon& commonProps, const EncoderInfo& encoderInfo)
    : commonProps(commonProps), encoderInfo(encoderInfo) {
    InitDefaults();
}

void UISettingsController::Load(IPropertyProvider* values) {
    uint8_t val8 = 0;
    values->GetUINT8("ffmpeg_reset", val8);
    if (val8 != 0) {
        *this = UISettingsController(encoderInfo);
        return;
    }

    int32_t val32 = 0;
    values->GetINT32(qualityModeId.c_str(), val32);
    qualityMode = static_cast<QualityMode>(val32);
    SetFirstSupportedQualityMode();

    values->GetINT32(qpId.c_str(), qp);
    values->GetINT32(bitrateId.c_str(), bitRate);
    values->GetINT32(presetId.c_str(), preset);
    values->GetINT32(profileId.c_str(), profile);
    values->GetINT32(levelId.c_str(), level);
    values->GetINT32(gopId.c_str(), gop);

    std::string customParamsStr;
    if (values->GetString(customParamsId.c_str(), customParamsStr)) {
        customParams = customParamsStr;
    }
}

StatusCode UISettingsController::Render(HostListRef* settingsList) const {
    StatusCode err = RenderQuality(settingsList);
    if (err != errNone) return err;

    {
        HostUIConfigEntryRef item("ffmpeg_reset");
        item.MakeButton("Reset");
        item.SetTriggersUpdate(true);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate the button entry");
            return errFail;
        }
    }

    return errNone;
}

void UISettingsController::InitDefaults() {
    qualityMode = CRF;
    SetFirstSupportedQualityMode();

    qp      = encoderInfo.qp[1];
    bitRate = 6000;
    preset  = encoderInfo.defaultPreset;
    profile = -1;   // Auto
    level   = -1;   // Auto
    gop     = 30;   // 30 fotogramas

    const std::string prefix = std::string("ffmpeg_") + encoderInfo.encoder + "_";
    qualityModeId  = prefix + "q_mode";
    qpId           = prefix + "qp";
    bitrateId      = prefix + "bitrate";
    presetId       = prefix + "preset";
    customParamsId = prefix + "custom_params";
    profileId      = prefix + "profile";
    levelId        = prefix + "level";
    gopId          = prefix + "gop";
}

void UISettingsController::SetFirstSupportedQualityMode() {
    if (!(qualityMode & encoderInfo.qualityModes)) {
        if (encoderInfo.qualityModes & CRF)       qualityMode = CRF;
        else if (encoderInfo.qualityModes & CQP)  qualityMode = CQP;
        else if (encoderInfo.qualityModes & VBR)  qualityMode = VBR;
        else if (encoderInfo.qualityModes & CBR)  qualityMode = CBR;
    }
}

StatusCode UISettingsController::RenderQuality(HostListRef* settingsList) const {

    // --- Encoder Preset ---
    {
        HostUIConfigEntryRef item(presetId);
        std::vector<std::string> textsVec;
        std::vector<int> valuesVec;
        for (const auto& [key, value] : encoderInfo.presets) {
            valuesVec.push_back(key);
            textsVec.emplace_back(value);
        }
        item.MakeComboBox("Encoder Preset", textsVec, valuesVec, preset);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate encoder preset UI entry");
            return errFail;
        }
    }

    // --- Quality Control ---
    {
        HostUIConfigEntryRef item(qualityModeId);
        std::vector<std::string> textsVec;
        std::vector<int> valuesVec;

        if (encoderInfo.qualityModes & CRF) { textsVec.emplace_back("Constant Rate Factor"); valuesVec.push_back(CRF); }
        if (encoderInfo.qualityModes & CQP) { textsVec.emplace_back("Constant Quality");     valuesVec.push_back(CQP); }
        if (encoderInfo.qualityModes & VBR) { textsVec.emplace_back("Variable Rate");        valuesVec.push_back(VBR); }
        if (encoderInfo.qualityModes & CBR) { textsVec.emplace_back("Constant Bit Rate (CBR)"); valuesVec.push_back(CBR); }

        item.MakeRadioBox("Quality Control", textsVec, valuesVec, qualityMode);
        item.SetTriggersUpdate(true);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate quality UI entry");
            return errFail;
        }
    }

    // --- Factor / QP slider (oculto en VBR y CBR) ---
    {
        HostUIConfigEntryRef item(qpId);
        const char* pLabel = nullptr;
        if (encoderInfo.hwAcceleration == Nvenc && qp == 0)   pLabel = "(automatic)";
        else if (qp < encoderInfo.qp[2] / 3)                  pLabel = "(high)";
        else if (qp < encoderInfo.qp[2] * 2 / 3)              pLabel = "(medium)";
        else                                                   pLabel = "(low)";

        item.MakeSlider("Factor", pLabel, qp, encoderInfo.qp[0], encoderInfo.qp[2], encoderInfo.qp[1]);
        item.SetTriggersUpdate(true);
        item.SetHidden(qualityMode == VBR || qualityMode == CBR);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate qp slider UI entry");
            return errFail;
        }
    }

    // --- Bit Rate slider (visible en VBR y CBR) ---
    {
        HostUIConfigEntryRef item(bitrateId);
        item.MakeSlider("Bit Rate", "kb/s", bitRate, 100, 100000, 1);
        item.SetHidden(qualityMode != VBR && qualityMode != CBR);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate bitrate slider UI entry");
            return errFail;
        }
    }

    // --- Separador antes de controles avanzados ---
    {
        HostUIConfigEntryRef sep("sep_advanced");
        sep.MakeSeparator();
        if (!sep.IsSuccess() || !settingsList->Append(&sep)) return errFail;
    }

    // --- Perfil del encoder ---
    // -1=Auto, 66=Baseline, 77=Main, 100=High
    // Valores estándar H.264 (ITU-T H.264 Annex A)
    {
        HostUIConfigEntryRef item(profileId);
        item.MakeRadioBox("Encoder Profile",
            {"Auto", "Baseline", "Main", "High"},
            {-1, 66, 77, 100},
            profile);
        item.SetTriggersUpdate(true);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate profile UI entry");
            return errFail;
        }
    }

    // --- Nivel del encoder ---
    // -1=Auto, valores x10 para evitar floats: 41=4.1, 42=4.2 ... 52=5.2
    {
        HostUIConfigEntryRef item(levelId);
        item.MakeRadioBox("Encoder Level",
            {"Auto", "4.1  (Full HD)", "4.2  (Full HD HFR)", "5.0  (4K)", "5.1  (4K)", "5.2  (4K HFR)"},
            {-1, 41, 42, 50, 51, 52},
            level);
        item.SetTriggersUpdate(true);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate level UI entry");
            return errFail;
        }
    }

    // --- GOP (fotogramas clave) ---
    {
        HostUIConfigEntryRef item(gopId);
        item.MakeSlider("Keyframe Interval", "frames", gop, 1, 300, 30);
        item.SetTriggersUpdate(false);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate GOP UI entry");
            return errFail;
        }
    }

    // --- Encoder Params (custom) ---
    if (encoderInfo.customParamsKey != nullptr) {
        {
            HostUIConfigEntryRef sep("separator");
            sep.MakeSeparator();
            if (!sep.IsSuccess() || !settingsList->Append(&sep)) return errFail;
        }
        HostUIConfigEntryRef item(customParamsId);
        item.MakeTextBox("Encoder Params", customParams, "");
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate custom params UI entry");
            return errFail;
        }
    }

    return errNone;
}

QualityMode UISettingsController::GetQualityMode() const { return qualityMode; }
int32_t UISettingsController::GetQP() const      { return std::max<int>(0, qp); }
int32_t UISettingsController::GetBitRate() const  { return bitRate * 1000; }
int32_t UISettingsController::GetPreset() const   { return preset; }
int32_t UISettingsController::GetProfile() const  { return profile; }
int32_t UISettingsController::GetLevel() const    { return level; }
int32_t UISettingsController::GetGOP() const      { return gop; }
const std::string& UISettingsController::GetCustomParams() const { return customParams; }
