#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

class JobRunner final : private juce::Thread
{
public:
    using ProgressCallback = std::function<void(const juce::String&)>;
    using CompletionCallback = std::function<void(const juce::File&, const juce::String&)>;

    JobRunner();
    ~JobRunner() override;

    bool start(const juce::StringArray& command,
               const juce::File& outputDirectory,
               ProgressCallback progress,
               CompletionCallback completion);
    void cancel();
    bool isActive() const noexcept { return running.load(); }

private:
    void run() override;
    juce::File newestCompletedRun() const;
    void publishOutput(const juce::String& chunk);

    juce::ChildProcess process;
    juce::StringArray pendingCommand;
    juce::File outputDirectory;
    ProgressCallback progressCallback;
    CompletionCallback completionCallback;
    std::atomic<bool> running { false };
    juce::String bufferedOutput;
    juce::String diagnosticOutput;
    juce::Time jobStartedAt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JobRunner)
};
