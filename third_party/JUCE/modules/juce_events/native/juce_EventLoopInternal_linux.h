/*
  Plasma backport of JUCE 7's Linux event loop hooks, so plugin wrappers (clap-juce-extensions) can
  hand JUCE's file descriptors to the host's event loop instead of polling on a timer.
*/

#pragma once

namespace juce
{
namespace LinuxEventLoopInternal
{
    /** Receives a callback when the set of file descriptors JUCE wants watched changes. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void fdCallbacksChanged() = 0;
    };

    void registerLinuxEventLoopListener (Listener&);
    void deregisterLinuxEventLoopListener (Listener&);

    /** Runs the callback registered for this file descriptor. Call it on the message thread when
        the host reports the descriptor as readable. */
    void invokeEventLoopCallbackForFd (int);

    std::vector<int> getRegisteredFds();

    /** Runs every callback whose descriptor is readable right now; returns false if none was. */
    bool dispatchPendingEvents();
}
}
