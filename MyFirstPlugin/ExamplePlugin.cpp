/*
 * DISTRHO Plugin Framework (DPF)
 * Copyright (C) 2012-2024 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "DistrhoPlugin.hpp"
#include "MidiMonitor.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

START_NAMESPACE_DISTRHO

// -----------------------------------------------------------------------------------------------------------

/**
  Plugin to demonstrate parameter outputs using meters.
 */
class ExamplePluginMeters : public MidiMonitorPlugin
{
public:
    ExamplePluginMeters()
        : MidiMonitorPlugin(3, 0, 0), // 3 parameters, 0 programs, 0 states
          fColor(0.0f),
          fOutLeft(0.0f),
          fOutRight(0.0f)
    {
    }
  
    bool popMidiEvent(MidiMonitorEvent& event) noexcept override
    {
        const uint32_t tail = fMidiTail.load(std::memory_order_relaxed);
        const uint32_t head = fMidiHead.load(std::memory_order_acquire);

        if (tail == head)
            return false;

        event = fMidiBuffer[tail];

        fMidiTail.store((tail + 1) % MIDI_BUFFER_SIZE,
                        std::memory_order_release);

        return true;
    }  

protected:
   /* --------------------------------------------------------------------------------------------------------
    * Information */

   /**
      Get the plugin label.
      A plugin label follows the same rules as Parameter::symbol, with the exception that it can start with numbers.
    */
    const char* getLabel() const override
    {
        return "meters";
    }

   /**
      Get an extensive comment/description about the plugin.
    */
    const char* getDescription() const override
    {
        return "Plugin to demonstrate parameter outputs using meters.";
    }

   /**
      Get the plugin author/maker.
    */
    const char* getMaker() const override
    {
        return "DISTRHO";
    }

   /**
      Get the plugin homepage.
    */
    const char* getHomePage() const override
    {
        return "https://github.com/DISTRHO/DPF";
    }

   /**
      Get the plugin license name (a single line of text).
      For commercial plugins this should return some short copyright information.
    */
    const char* getLicense() const override
    {
        return "ISC";
    }

   /**
      Get the plugin version, in hexadecimal.
    */
    uint32_t getVersion() const override
    {
        return d_version(1, 0, 0);
    }

   /* --------------------------------------------------------------------------------------------------------
    * Init */

   /**
      Initialize the audio port @a index.@n
      This function will be called once, shortly after the plugin is created.
    */
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        // treat meter audio ports as stereo
        port.groupId = kPortGroupStereo;

        // everything else is as default
        Plugin::initAudioPort(input, index, port);
    }

   /**
      Initialize the parameter @a index.@n
      This function will be called once, shortly after the plugin is created.
    */
    void initParameter(uint32_t index, Parameter& parameter) override
    {
       /**
          All parameters in this plugin have the same ranges.
        */
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 1.0f;
        parameter.ranges.def = 0.0f;

       /**
          Set parameter data.
        */
        switch (index)
        {
        case 0:
            parameter.hints  = kParameterIsAutomatable|kParameterIsInteger;
            parameter.name   = "color";
            parameter.symbol = "color";
            parameter.enumValues.count = 2;
            parameter.enumValues.restrictedMode = true;
            {
                ParameterEnumerationValue* const values = new ParameterEnumerationValue[2];
                parameter.enumValues.values = values;

                values[0].label = "Green";
                values[0].value = METER_COLOR_GREEN;
                values[1].label = "Blue";
                values[1].value = METER_COLOR_BLUE;
            }
            break;
        case 1:
            parameter.hints  = kParameterIsAutomatable|kParameterIsOutput;
            parameter.name   = "out-left";
            parameter.symbol = "out_left";
            break;
        case 2:
            parameter.hints  = kParameterIsAutomatable|kParameterIsOutput;
            parameter.name   = "out-right";
            parameter.symbol = "out_right";
            break;
        }
    }

   /**
      Set a state key and default value.
      This function will be called once, shortly after the plugin is created.
    */
    void initState(uint32_t, String&, String&) override
    {
        // we are using states but don't want them saved in the host
    }

   /* --------------------------------------------------------------------------------------------------------
    * Internal data */

   /**
      Get the current value of a parameter.
    */
    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case 0: return fColor;
        case 1: return fOutLeft;
        case 2: return fOutRight;
        }

        return 0.0f;
    }

   /**
      Change a parameter value.
    */
    void setParameterValue(uint32_t index, float value) override
    {
        // this is only called for input paramters, and we only have one of those.
        if (index != 0) return;

        fColor = value;
    }

   /* --------------------------------------------------------------------------------------------------------
    * Process */

   /**
      Run/process function for plugins without MIDI input.
    */
  void run(const float** inputs,
	   float** outputs,
	   uint32_t frames,
	   const MidiEvent* midiEvents,
	   uint32_t midiEventCount) override  
    {

      // Capture incoming MIDI note events.
      for (uint32_t i = 0; i < midiEventCount; ++i)
        {
            const MidiEvent& ev = midiEvents[i];

            if (ev.size < 3)
                continue;

            const uint8_t status = ev.data[0] & 0xF0;

            if (status != 0x80 && status != 0x90)
                continue;

            MidiMonitorEvent event {
                ev.data[0],
                ev.data[1],
                ev.data[2]
            };

            pushMidiEvent(event);
        }
      
        float tmp;
        float tmpLeft  = 0.0f;
        float tmpRight = 0.0f;

        for (uint32_t i=0; i<frames; ++i)
        {
            // left
            tmp = std::abs(inputs[0][i]);

            if (tmp > tmpLeft)
                tmpLeft = tmp;

            // right
            tmp = std::abs(inputs[1][i]);

            if (tmp > tmpRight)
                tmpRight = tmp;
        }

        if (tmpLeft > 1.0f)
            tmpLeft = 1.0f;
        if (tmpRight > 1.0f)
            tmpRight = 1.0f;

	        // Peak with ~300 ms decay so the bar falls back down.
        const float decay = std::exp(-static_cast<float>(frames)
                                     / (static_cast<float>(getSampleRate()) * 0.3f));

        fOutLeft  = std::max(tmpLeft,  fOutLeft  * decay);
        fOutRight = std::max(tmpRight, fOutRight * decay);

        // copy inputs over outputs if needed
        if (outputs[0] != inputs[0])
            std::memcpy(outputs[0], inputs[0], sizeof(float)*frames);

        if (outputs[1] != inputs[1])
            std::memcpy(outputs[1], inputs[1], sizeof(float)*frames);

        // Mix the sine voice on top of the passthrough audio,
        // splitting the block at each MIDI event for sample-accurate timing.
        uint32_t pos = 0;

        for (uint32_t i = 0; i < midiEventCount; ++i)
        {
            const uint32_t frame = midiEvents[i].frame < frames ? midiEvents[i].frame : frames;

            renderSine(outputs, pos, frame);
            handleNote(midiEvents[i]);
            pos = frame;
        }

        renderSine(outputs, pos, frames);	
    }

    // -------------------------------------------------------------------------------------------------------

private:
   /**
      Parameters.
    */
    float fColor, fOutLeft, fOutRight;

   /**
      Boolean used to reset meter values.
      The UI will send a "reset" message which sets this as true.
    */

    static constexpr uint32_t MIDI_BUFFER_SIZE = 64;

    MidiMonitorEvent fMidiBuffer[MIDI_BUFFER_SIZE];

    std::atomic<uint32_t> fMidiHead { 0 };
    std::atomic<uint32_t> fMidiTail { 0 };

    // Sine voice state (audio thread only)
    double fPhase     = 0.0;
    float  fFreq      = 440.0f;
    float  fTargetAmp = 0.0f;
    float  fEnv       = 0.0f;
    int    fNote      = -1;

    void handleNote(const MidiEvent& ev) noexcept
    {
        if (ev.size < 3)
            return;

        const uint8_t status = ev.data[0] & 0xF0;
        const uint8_t note   = ev.data[1];
        const uint8_t vel    = ev.data[2];

        if (status == 0x90 && vel > 0)
        {
            fNote      = note;
            fFreq      = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
            fTargetAmp = (vel / 127.0f) * 0.25f;
        }
        else if (status == 0x80 || (status == 0x90 && vel == 0))
        {
            // only release if it's the note currently sounding
            if (note == fNote)
            {
                fNote      = -1;
                fTargetAmp = 0.0f;
            }
        }
    }

    void renderSine(float** outputs, uint32_t start, uint32_t end) noexcept
    {
        const double inc = fFreq / getSampleRate();

        for (uint32_t i = start; i < end; ++i)
        {
            // one-pole smoothing for attack/release
            fEnv += (fTargetAmp - fEnv) * 0.002f;

            const float s = static_cast<float>(std::sin(fPhase * 2.0 * M_PI)) * fEnv;

            fPhase += inc;
            if (fPhase >= 1.0)
                fPhase -= 1.0;

            outputs[0][i] += s;
            outputs[1][i] += s;
        }
    }  

    void pushMidiEvent(const MidiMonitorEvent& event) noexcept
    {
        const uint32_t head = fMidiHead.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) % MIDI_BUFFER_SIZE;

        // Drop the newest event if the buffer is full.
        if (next == fMidiTail.load(std::memory_order_acquire))
            return;

        fMidiBuffer[head] = event;

        fMidiHead.store(next, std::memory_order_release);
    }  

   /**
      Set our plugin class as non-copyable and add a leak detector just in case.
    */
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExamplePluginMeters)
};

/* ------------------------------------------------------------------------------------------------------------
 * Plugin entry point, called by DPF to create a new plugin instance. */

Plugin* createPlugin()
{
    return new ExamplePluginMeters();
}

// -----------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
