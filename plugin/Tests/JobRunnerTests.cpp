#include "JobRunner.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>

namespace
{
bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (predicate()) return true;
        juce::Thread::sleep(5);
    }
    return predicate();
}

bool processExists(int processId)
{
    if (processId <= 0) return false;
    errno = 0;
    return ::kill(processId, 0) == 0 || errno != ESRCH;
}

int fail(const char* message)
{
    std::cerr << message << std::endl;
    return EXIT_FAILURE;
}
}

int main()
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("diverge-job-runner-soak", {}, false);
    if (!root.createDirectory()) return fail("could not create soak directory");

    for (int iteration = 0; iteration < 12; ++iteration)
    {
        const auto pidFile = root.getChildFile("child-" + juce::String(iteration) + ".pid");
        const auto script = "echo $$ > " + pidFile.getFullPathName().quoted()
                            + "; exec sleep 30";
        JobRunner runner;
        if (!runner.start({ "/bin/sh", "-c", script }, root))
            return fail("runner rejected a fresh job");
        if (!waitUntil([&pidFile] { return pidFile.existsAsFile(); }, 2000))
            return fail("child process did not start");
        const auto processId = pidFile.loadFileAsString().trim().getIntValue();
        if (!processExists(processId)) return fail("child PID was not alive before cancellation");
        runner.cancel();
        runner.cancel();
        if (runner.isActive()) return fail("runner remained active after cancellation");
        if (runner.snapshot().status != JobRunner::Status::cancelled)
            return fail("runner did not publish cancellation");
        if (!waitUntil([processId] { return !processExists(processId); }, 2000))
            return fail("cancelled child process survived");
    }

    root.deleteRecursively();
    return EXIT_SUCCESS;
}
