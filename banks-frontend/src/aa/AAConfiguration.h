#pragma once

#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>

namespace aa {

class AAConfiguration : public f1x::openauto::autoapp::configuration::IConfiguration {
public:
    void load() override {}
    void reset() override {}
    void save() override {}

    bool hasTouchScreen() const override { return true; }

    void setHandednessOfTrafficType(f1x::openauto::autoapp::configuration::HandednessOfTrafficType) override {}
    f1x::openauto::autoapp::configuration::HandednessOfTrafficType getHandednessOfTrafficType() const override {
        return f1x::openauto::autoapp::configuration::HandednessOfTrafficType::LEFT_HAND_DRIVE;
    }

    void showClock(bool) override {}
    bool showClock() const override { return false; }
    void showBigClock(bool) override {}
    bool showBigClock() const override { return false; }
    void oldGUI(bool) override {}
    bool oldGUI() const override { return false; }
    void setAlphaTrans(size_t) override {}
    size_t getAlphaTrans() const override { return 100; }
    void hideMenuToggle(bool) override {}
    bool hideMenuToggle() const override { return true; }
    void hideAlpha(bool) override {}
    bool hideAlpha() const override { return true; }
    void showLux(bool) override {}
    bool showLux() const override { return false; }
    void showCursor(bool) override {}
    bool showCursor() const override { return false; }
    void hideBrightnessControl(bool) override {}
    bool hideBrightnessControl() const override { return true; }
    void showNetworkinfo(bool) override {}
    bool showNetworkinfo() const override { return false; }
    void hideWarning(bool) override {}
    bool hideWarning() const override { return true; }

    std::string getMp3MasterPath() const override { return {}; }
    void setMp3MasterPath(const std::string&) override {}
    std::string getMp3SubFolder() const override { return {}; }
    void setMp3SubFolder(const std::string&) override {}
    int32_t getMp3Track() const override { return 0; }
    void setMp3Track(int32_t) override {}
    bool mp3AutoPlay() const override { return false; }
    void mp3AutoPlay(bool) override {}
    bool showAutoPlay() const override { return false; }
    void showAutoPlay(bool) override {}
    bool instantPlay() const override { return false; }
    void instantPlay(bool) override {}

    QString getCSValue(QString) const override { return {}; }
    QString readFileContent(QString) const override { return {}; }
    QString getParamFromFile(QString, QString) const override { return {}; }

    aap_protobuf::service::media::sink::message::VideoFrameRateType getVideoFPS() const override {
        return aap_protobuf::service::media::sink::message::VideoFrameRateType::VIDEO_FPS_60;
    }
    void setVideoFPS(aap_protobuf::service::media::sink::message::VideoFrameRateType) override {}

    aap_protobuf::service::media::sink::message::VideoCodecResolutionType getVideoResolution() const override {
        return aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_1920x1080;
    }
    void setVideoResolution(aap_protobuf::service::media::sink::message::VideoCodecResolutionType) override {}

    size_t getScreenDPI() const override { return 140; }
    void setScreenDPI(size_t) override {}
    void setOMXLayerIndex(int32_t) override {}
    int32_t getOMXLayerIndex() const override { return 0; }
    void setVideoMargins(QRect) override {}
    QRect getVideoMargins() const override { return {0, 0, 0, 0}; }

    bool getTouchscreenEnabled() const override { return true; }
    void setTouchscreenEnabled(bool) override {}
    bool playerButtonControl() const override { return false; }
    void playerButtonControl(bool) override {}

    ButtonCodes getButtonCodes() const override {
        using KC = aap_protobuf::service::media::sink::message::KeyCode;
        return { KC::KEYCODE_HOME, KC::KEYCODE_BACK, KC::KEYCODE_SEARCH };
    }
    void setButtonCodes(const ButtonCodes&) override {}

    f1x::openauto::autoapp::configuration::BluetoothAdapterType getBluetoothAdapterType() const override {
        return f1x::openauto::autoapp::configuration::BluetoothAdapterType::NONE;
    }
    void setBluetoothAdapterType(f1x::openauto::autoapp::configuration::BluetoothAdapterType) override {}
    std::string getBluetoothAdapterAddress() const override { return {}; }
    void setBluetoothAdapterAddress(const std::string&) override {}
    bool getWirelessProjectionEnabled() const override { return true; }
    void setWirelessProjectionEnabled(bool) override {}

    bool musicAudioChannelEnabled() const override { return true; }
    void setMusicAudioChannelEnabled(bool) override {}
    bool guidanceAudioChannelEnabled() const override { return true; }
    void setGuidanceAudioChannelEnabled(bool) override {}
    bool systemAudioChannelEnabled() const override { return true; }
    void setSystemAudioChannelEnabled(bool) override {}
    bool telephonyAudioChannelEnabled() const override { return false; }
    void setTelephonyAudioChannelEnabled(bool) override {}

    f1x::openauto::autoapp::configuration::AudioOutputBackendType getAudioOutputBackendType() const override {
        return f1x::openauto::autoapp::configuration::AudioOutputBackendType::QT;
    }
    void setAudioOutputBackendType(f1x::openauto::autoapp::configuration::AudioOutputBackendType) override {}
};

}
