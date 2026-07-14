#include "JobRunner.h"

JobRunner::JobRunner() : juce::Thread("Diverge CLI job") {}

JobRunner::~JobRunner()
{
    cancel();
}

bool JobRunner::start(const juce::StringArray& command, const juce::File& output)
{
    if (running.exchange(true))
        return false;
    pendingCommand = command;
    outputDirectory = output;
    bufferedOutput.clear();
    diagnosticOutput.clear();
    jobStartedAt = juce::Time::getCurrentTime();
    updateSnapshot([](Snapshot& state)
    {
        state = {};
        state.status = Status::preparing;
        state.message = "Preparing your source";
    });
    startThread();
    return true;
}

void JobRunner::cancel()
{
    const auto wasRunning = running.load();
    signalThreadShouldExit();
    if (process.isRunning())
        process.kill();
    stopThread(5000);
    running = false;
    if (wasRunning)
        updateSnapshot([](Snapshot& state)
        {
            state.status = Status::cancelled;
            state.message = "Creation cancelled - your previous results are safe";
        });
}

JobRunner::Snapshot JobRunner::snapshot() const
{
    const juce::ScopedLock lock(snapshotLock);
    return currentSnapshot;
}

void JobRunner::updateSnapshot(std::function<void(Snapshot&)> update)
{
    const juce::ScopedLock lock(snapshotLock);
    update(currentSnapshot);
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
    {
        if (line.startsWith("PROGRESS"))
        {
            const auto parts = juce::StringArray::fromTokens(line.fromFirstOccurrenceOf(" ", false, false), "/", "");
            const auto completed = parts.size() > 0 ? parts[0].getIntValue() : 0;
            const auto total = parts.size() > 1 ? parts[1].getIntValue() : 0;
            updateSnapshot([completed, total](Snapshot& state)
            {
                state.status = completed < total ? Status::creating : Status::comparing;
                state.completed = completed;
                state.total = total;
                state.message = completed < total ? "Creating candidates" : "Comparing the full set";
            });
        }
        else if (line.startsWith("BATCH_RETRY"))
            updateSnapshot([](Snapshot& state) { state.message = "Adapting to available memory"; });
    }
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
    updateSnapshot([result, error](Snapshot& state)
    {
        state.run = result;
        state.error = error;
        if (result.isDirectory())
        {
            state.status = Status::complete;
            state.message = "Eight variations ready";
        }
        else if (state.status != Status::cancelled)
        {
            state.status = Status::failed;
            state.message = "Creation needs attention";
        }
    });
}
