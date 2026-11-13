#pragma once

#include "../Core/CommonTypes.h"
#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>
#include <atomic>

namespace MScoreVideoSync
{

class NetworkBridge : public juce::Thread,
                      private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    class CommandListener
    {
    public:
        virtual ~CommandListener() = default;
        virtual void onRemotePlay() = 0;
        virtual void onRemotePause() = 0;
        virtual void onRemoteStop() = 0;
        virtual void onRemoteLocate(double timeSeconds) = 0;
    };

    NetworkBridge();
    ~NetworkBridge() override;

    bool startBroadcaster(const juce::String& targetHost = "127.0.0.1", int targetPort = 9000);
    bool startReceiver(int listenPort = 9001);
    void stopBridge();

    void setCommandListener(CommandListener* listener) noexcept { commandListener_ = listener; }

    // REAL-TIME AUDIO SAFE: Stores latest snapshot into lock-free atomics
    void updateTelemetry(const TransportSnapshot& snapshot) noexcept;

    // Send remote command from Standalone Video Pro back to Plugin / MuseScore
    bool sendRemotePlay();
    bool sendRemotePause();
    bool sendRemoteStop();
    bool sendRemoteLocate(double timeSeconds);

private:
    void run() override;

    // juce::OSCReceiver::Listener override
    void oscMessageReceived(const juce::OSCMessage& message) override;

    juce::OSCSender oscSender_;
    juce::OSCReceiver oscReceiver_;

    std::atomic<bool> isBroadcasting_{ false };
    std::atomic<bool> isReceiving_{ false };

    // Atomic telemetry cache for thread transmission
    std::atomic<bool> isPlaying_{ false };
    std::atomic<double> timeInSeconds_{ 0.0 };
    std::atomic<double> bpm_{ 120.0 };
    std::atomic<double> ppqPosition_{ 0.0 };
    std::atomic<int> bar_{ 1 };
    std::atomic<double> beat_{ 1.0 };
    std::atomic<int> timeSigNum_{ 4 };
    std::atomic<int> timeSigDenom_{ 4 };

    CommandListener* commandListener_{ nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkBridge)
};

} // namespace MScoreVideoSync
