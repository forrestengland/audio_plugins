
#ifndef MIDI_MONITOR_HPP_INCLUDED
#define MIDI_MONITOR_HPP_INCLUDED

#include "DistrhoPlugin.hpp"
#include <cstdint>

START_NAMESPACE_DISTRHO

struct MidiMonitorEvent
{
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

class MidiMonitorAccess
{
public:
    virtual ~MidiMonitorAccess() = default;

    virtual bool popMidiEvent(MidiMonitorEvent& event) noexcept = 0;
};

class MidiMonitorPlugin : public Plugin, public MidiMonitorAccess
{
public:
    MidiMonitorPlugin(uint32_t params, uint32_t programs, uint32_t states)
        : Plugin(params, programs, states) {}
};

END_NAMESPACE_DISTRHO

#endif
