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
#include <vector>
#include <string>
#include <cstdlib>
#include <cctype>

#ifdef _WIN32
 #include <direct.h>
 #define MKDIR(p) _mkdir(p)
#else
 #include <sys/stat.h>
 #define MKDIR(p) mkdir(p, 0755)
#endif

START_NAMESPACE_DISTRHO

// -----------------------------------------------------------------------------------------------------------

/**
  Plugin to demonstrate parameter outputs using meters.
 */
class ExamplePluginMeters : public MidiMonitorPlugin
{
public:
    ExamplePluginMeters()
        : MidiMonitorPlugin(4, 0, 3), // 3 parameters, 0 programs, 0 states
          fColor(0.0f),
          fOutLeft(0.0f),
          fOutRight(0.0f),
	  fNeedsReset(true)
    {
      fRecCapacity = static_cast<uint32_t>(getSampleRate() * 30.0); // 30 s
      fRecL.assign(fRecCapacity, 0.0f);
      fRecR.assign(fRecCapacity, 0.0f); 
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
        case 3:
            parameter.hints  = kParameterIsAutomatable|kParameterIsBoolean;
            parameter.name   = "record";
            parameter.symbol = "record";
            break;
        }
    }

   /**
      Set a state key and default value.
      This function will be called once, shortly after the plugin is created.
    */
    void initState(uint32_t index, String& key, String& defaultValue) override
    {
        switch (index)
        {
        case 0: key = "name";   defaultValue = "sample"; break;
        case 1: key = "folder"; defaultValue = "";       break;
        case 2: key = "save";   defaultValue = "";       break;
        }
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
        case 3: return fRecording.load() ? 1.0f : 0.0f;	  
        }

        return 0.0f;
    }

   /**
      Change a parameter value.
    */
    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case 0: fColor = value; break;
        case 3: fRecording.store(value > 0.5f); break;
        }
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "name") == 0)
            fName = value;
        else if (std::strcmp(key, "folder") == 0)
            fFolder = value;
        else if (std::strcmp(key, "save") == 0)
            saveRecording();
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

        // Record input into RAM (audio thread: copy only, no I/O).
        const bool rec = fRecording.load(std::memory_order_relaxed);
        if (rec && !fWasRecording)
            fRecLen.store(0, std::memory_order_release);
        fWasRecording = rec;

        if (rec)
        {
            const uint32_t len  = fRecLen.load(std::memory_order_relaxed);
            const uint32_t room = fRecCapacity - len;
            const uint32_t n    = frames < room ? frames : room;

            if (n > 0)
            {
                std::memcpy(&fRecL[len], inputs[0], n * sizeof(float));
                std::memcpy(&fRecR[len], inputs[1], n * sizeof(float));
                fRecLen.store(len + n, std::memory_order_release);
            }
        }	

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
  bool fNeedsReset;

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

    std::vector<float> fRecL, fRecR;
    uint32_t fRecCapacity = 0;
    std::atomic<bool>     fRecording { false };
    std::atomic<uint32_t> fRecLen    { 0 };
    bool fWasRecording = false;

    std::string fName { "sample" };
    std::string fFolder;

    static void writeLE(std::FILE* f, uint32_t v, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
            std::fputc((v >> (8 * i)) & 0xFF, f);
    }

    static std::string sanitize(const std::string& in)
    {
        std::string out;
        for (char c : in)
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ')
                out += c;
        return out.empty() ? "sample" : out;
    }

    void saveRecording()
    {
        // Guards: nothing recorded (also covers hosts replaying states on load),
        // or still recording.
        const uint32_t len = fRecLen.load(std::memory_order_acquire);
        if (len == 0 || fRecording.load())
            return;

        std::string folder = fFolder;
        if (folder.empty())
        {
            const char* home = std::getenv("HOME");
            if (home == nullptr) home = std::getenv("USERPROFILE");
            folder = std::string(home ? home : ".") + "/Samples";
        }
        MKDIR(folder.c_str()); // fine if it already exists

        // Don't overwrite: name.wav, name_2.wav, ...
        const std::string base = folder + "/" + sanitize(fName);
        std::string path = base + ".wav";
        for (int n = 2; ; ++n)
        {
            std::FILE* t = std::fopen(path.c_str(), "rb");
            if (t == nullptr) break;
            std::fclose(t);
            path = base + "_" + std::to_string(n) + ".wav";
        }

        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f == nullptr)
        {
            std::printf("Could not write %s\n", path.c_str());
            return;
        }

        const uint32_t rate     = static_cast<uint32_t>(getSampleRate());
        const uint32_t dataSize = len * 2 * 2; // frames * channels * 16-bit

        std::fwrite("RIFF", 1, 4, f); writeLE(f, 36 + dataSize, 4);
        std::fwrite("WAVE", 1, 4, f);
        std::fwrite("fmt ", 1, 4, f); writeLE(f, 16, 4);
        writeLE(f, 1, 2);            // PCM
        writeLE(f, 2, 2);            // channels
        writeLE(f, rate, 4);
        writeLE(f, rate * 4, 4);     // byte rate
        writeLE(f, 4, 2);            // block align
        writeLE(f, 16, 2);           // bits
        std::fwrite("data", 1, 4, f); writeLE(f, dataSize, 4);

        for (uint32_t i = 0; i < len; ++i)
        {
            const float l = std::max(-1.0f, std::min(1.0f, fRecL[i]));
            const float r = std::max(-1.0f, std::min(1.0f, fRecR[i]));
            writeLE(f, static_cast<uint16_t>(static_cast<int16_t>(std::lrintf(l * 32767.0f))), 2);
            writeLE(f, static_cast<uint16_t>(static_cast<int16_t>(std::lrintf(r * 32767.0f))), 2);
        }

        std::fclose(f);
        std::printf("Saved %s (%u frames)\n", path.c_str(), len);
    }  

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
