#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

class JobRunner final : private juce::Thread
{
public:
    enum class Status { idle, preparing, creating, comparing, choosing, complete, cancelled, failed };
    struct Snapshot
    {
        Status status = Status::idle;
        juce::String message;
        int completed = 0;
        int total = 0;
        juce::File run;
        juce::String error;
    };

    JobRunner();
    ~JobRunner() override;

    bool start(const juce::StringArray& command, const juce::File& outputDirectory);
    void cancel();
    bool isActive() const noexcept { return running.load(); }
    Snapshot snapshot() const;

private:
    void run() override;
    juce::File newestCompletedRun() const;
    void publishOutput(const juce::String& chunk);
    void updateSnapshot(std::function<void(Snapshot&)> update);

    juce::ChildProcess process;
    juce::StringArray pendingCommand;
    juce::File outputDirectory;
    std::atomic<bool> running { false };
    juce::String bufferedOutput;
    juce::String diagnosticOutput;
    juce::Time jobStartedAt;
    mutable juce::CriticalSection snapshotLock;
    Snapshot currentSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JobRunner)
};
