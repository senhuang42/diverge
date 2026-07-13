#include "JobRunner.h"

JobRunner::JobRunner() : juce::Thread("Diverge CLI job") {}

JobRunner::~JobRunner()
{
    cancel();
}

bool JobRunner::start(const juce::StringArray& command,
                      const juce::File& output,
                      ProgressCallback progress,
                      CompletionCallback completion)
{
    if (running.exchange(true))
        return false;
    pendingCommand = command;
    outputDirectory = output;
    progressCallback = std::move(progress);
    completionCallback = std::move(completion);
    bufferedOutput.clear();
    diagnosticOutput.clear();
    jobStartedAt = juce::Time::getCurrentTime();
    startThread();
    return true;
}

void JobRunner::cancel()
{
    signalThreadShouldExit();
    if (process.isRunning())
        process.kill();
    stopThread(5000);
    running = false;
}

void JobRunner::publishOutput(const juce::String& chunk)
{
    diagnosticOutput += chunk;
    if (diagnosticOutput.length() > 16384)
        diagnosticOutput = diagnosticOutput.substring(diagnosticOutput.length() - 16384);
    bufferedOutput += chunk;
    auto lines = juce::StringArray::fromLines(bufferedOutput);
    if (!bufferedOutput.endsWithChar('\n') && lines.size() > 0)
    {
        bufferedOutput = lines[lines.size() - 1];
        lines.remove(lines.size() - 1);
    }
    else
        bufferedOutput.clear();

    for (const auto& line : lines)
        if (line.startsWith("PROGRESS") || line.startsWith("BATCH_RETRY"))
            juce::MessageManager::callAsync([callback = progressCallback, line]
            {
                if (callback)
                    callback(line);
            });
}

juce::File JobRunner::newestCompletedRun() const
{
    juce::Array<juce::File> directories;
    outputDirectory.findChildFiles(directories, juce::File::findDirectories, false);
    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b)
    {
        return a.getLastModificationTime() > b.getLastModificationTime();
    });
    for (const auto& directory : directories)
        if (directory.getChildFile("manifest.json").existsAsFile()
            && directory.getLastModificationTime() >= jobStartedAt)
            return directory;
    return {};
}

void JobRunner::run()
{
    juce::String error;
    if (!process.start(pendingCommand, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        error = "Could not start the configured Python process.";
    }
    else
    {
        std::array<char, 4096> bytes {};
        while (!threadShouldExit() && process.isRunning())
        {
            const auto count = process.readProcessOutput(bytes.data(), static_cast<int>(bytes.size()));
            if (count > 0)
                publishOutput(juce::String::fromUTF8(bytes.data(), count));
            else
                wait(30);
        }
        if (threadShouldExit() && process.isRunning())
            process.kill();
        const auto remaining = process.readAllProcessOutput();
        if (remaining.isNotEmpty())
            publishOutput(remaining + "\n");
        if (!threadShouldExit() && process.getExitCode() != 0)
        {
            const auto lines = juce::StringArray::fromLines(diagnosticOutput.trim());
            const auto detail = lines.isEmpty() ? juce::String("Unknown error") : lines[lines.size() - 1];
            error = "Generation failed: " + detail;
        }
    }

    const auto result = newestCompletedRun();
    running = false;
    juce::MessageManager::callAsync([callback = completionCallback, result, error]
    {
        if (callback)
            callback(result, error);
    });
}
