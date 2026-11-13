#include "NetworkBridge.h"

namespace MScoreVideoSync
{

NetworkBridge::NetworkBridge()
    : juce::Thread("NetworkBridgeOSCThread")
{
}

NetworkBridge::~NetworkBridge()
{
    stopBridge();
}

bool NetworkBridge::startBroadcaster(const juce::String& targetHost, int targetPort)
{
    if (oscSender_.connect(targetHost, targetPort))
    {
        isBroadcasting_.store(true, std::memory_order_release);
        if (!isThreadRunning())
        {
            startThread(juce::Thread::Priority::normal);
        }
        return true;
    }
    return false;
}

bool NetworkBridge::startReceiver(int listenPort)
{
    if (oscReceiver_.connect(listenPort))
    {
        oscReceiver_.addListener(this);
        isReceiving_.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

void NetworkBridge::stopBridge()
{
    isBroadcasting_.store(false, std::memory_order_release);
    signalThreadShouldExit();
    notify();
    stopThread(1000);

    if (isReceiving_.load(std::memory_order_acquire))
    {
        oscReceiver_.removeListener(this);
        oscReceiver_.disconnect();
        isReceiving_.store(false, std::memory_order_release);
    }

    oscSender_.disconnect();
}

void NetworkBridge::updateTelemetry(const TransportSnapshot& snapshot) noexcept
{
    isPlaying_.store(snapshot.isPlaying, std::memory_order_relaxed);
    timeInSeconds_.store(snapshot.timeInSeconds, std::memory_order_relaxed);
    bpm_.store(snapshot.bpm, std::memory_order_relaxed);
    ppqPosition_.store(snapshot.ppqPosition, std::memory_order_relaxed);
    bar_.store(snapshot.bar, std::memory_order_relaxed);
    beat_.store(snapshot.beat, std::memory_order_relaxed);
    timeSigNum_.store(snapshot.timeSigNumerator, std::memory_order_relaxed);
    timeSigDenom_.store(snapshot.timeSigDenominator, std::memory_order_relaxed);
}

void NetworkBridge::run()
{
    // Transmit telemetry at 60Hz rate
    while (!threadShouldExit())
    {
        if (isBroadcasting_.load(std::memory_order_relaxed))
        {
            bool playing = isPlaying_.load(std::memory_order_relaxed);
            float timeSec = static_cast<float>(timeInSeconds_.load(std::memory_order_relaxed));
            float bpm = static_cast<float>(bpm_.load(std::memory_order_relaxed));
            float ppq = static_cast<float>(ppqPosition_.load(std::memory_order_relaxed));
            int bar = bar_.load(std::memory_order_relaxed);
            float beat = static_cast<float>(beat_.load(std::memory_order_relaxed));
            int num = timeSigNum_.load(std::memory_order_relaxed);
            int den = timeSigDenom_.load(std::memory_order_relaxed);

            juce::OSCMessage msg("/mscore/transport",
                                 playing ? 1 : 0,
                                 timeSec,
                                 bpm,
                                 ppq,
                                 bar,
                                 beat,
                                 num,
                                 den);

            oscSender_.send(msg);
        }

        wait(16); // ~60 Hz
    }
}

bool NetworkBridge::sendRemotePlay()
{
    return oscSender_.send(juce::OSCMessage("/mscore/control/play"));
}

bool NetworkBridge::sendRemotePause()
{
    return oscSender_.send(juce::OSCMessage("/mscore/control/pause"));
}

bool NetworkBridge::sendRemoteStop()
{
    return oscSender_.send(juce::OSCMessage("/mscore/control/stop"));
}

bool NetworkBridge::sendRemoteLocate(double timeSeconds)
{
    return oscSender_.send(juce::OSCMessage("/mscore/control/locate", static_cast<float>(timeSeconds)));
}

void NetworkBridge::oscMessageReceived(const juce::OSCMessage& message)
{
    if (commandListener_ == nullptr)
        return;

    auto pattern = message.getAddressPattern().toString();
    if (pattern == "/mscore/control/play")
    {
        commandListener_->onRemotePlay();
    }
    else if (pattern == "/mscore/control/pause")
    {
        commandListener_->onRemotePause();
    }
    else if (pattern == "/mscore/control/stop")
    {
        commandListener_->onRemoteStop();
    }
    else if (pattern == "/mscore/control/locate" && message.size() >= 1)
    {
        if (message[0].isFloat32())
            commandListener_->onRemoteLocate(message[0].getFloat32());
    }
}

} // namespace MScoreVideoSync
