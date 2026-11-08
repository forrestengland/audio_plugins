// ensoniq mirage inspired sampler dpf plugin by Forrest England 2026

#include "DistrhoPlugin.hpp"
#include "MidiMonitor.hpp"
#include "WaveReader.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdlib>
#include <cctype>
#include <thread>

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
        : MidiMonitorPlugin(421, 0, 19),
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

        if (index == kLearnParam)
        {
            parameter.hints      = kParameterIsOutput|kParameterIsInteger;
            parameter.name       = "learn-note";
            parameter.symbol     = "learn_note";
            parameter.ranges.max = 1048575.0f;
            return;
        }

        if (index >= kPitchParam)
        {
            static const char* const names[] = { "fine", "detune", "lfo-rate" };
            static const char* const syms[]  = { "fine", "detune", "lfo_rate" };

            const uint32_t g = (index - kPitchParam) / 16;
            const uint32_t s = (index - kPitchParam) % 16;
            if (g > 2) return;

            char name[40], sym[40];
            std::snprintf(name, sizeof(name), "slot%u-%s", s + 1, names[g]);
            std::snprintf(sym,  sizeof(sym),  "s%u_%s",    s + 1, syms[g]);
            parameter.name   = name;
            parameter.symbol = sym;
            parameter.hints  = kParameterIsAutomatable;

            if (g == 0)
            {
                parameter.unit       = "st";
                parameter.ranges.min = -1.0f;
                parameter.ranges.max = 1.0f;
                parameter.ranges.def = 0.0f;
            }
            else if (g == 1)
            {
                parameter.unit       = "ct";
                parameter.ranges.min = 0.0f;
                parameter.ranges.max = 100.0f;
                parameter.ranges.def = 0.0f;
            }
            else
            {
                parameter.hints      = kParameterIsAutomatable|kParameterIsLogarithmic;
                parameter.unit       = "Hz";
                parameter.ranges.min = 0.1f;
                parameter.ranges.max = 20.0f;
                parameter.ranges.def = 5.0f;
            }
            return;
        }	

        if (index >= kFEnvParam)
        {
            static const char* const names[] = { "fenv-attack", "fenv-decay", "fenv-sustain",
                                                 "fenv-release", "fenv-amount" };
            static const char* const syms[]  = { "fenv_attack", "fenv_decay", "fenv_sustain",
                                                 "fenv_release", "fenv_amount" };

            const uint32_t g = (index - kFEnvParam) / 16;
            const uint32_t s = (index - kFEnvParam) % 16;
            if (g > 4) return;

            char name[48], sym[48];
            std::snprintf(name, sizeof(name), "slot%u-%s", s + 1, names[g]);
            std::snprintf(sym,  sizeof(sym),  "s%u_%s",    s + 1, syms[g]);
            parameter.name   = name;
            parameter.symbol = sym;

            if (g == 2)
            {
                parameter.hints      = kParameterIsAutomatable;
                parameter.ranges.def = 0.0f;
            }
            else if (g == 4)
            {
                parameter.hints      = kParameterIsAutomatable;
                parameter.ranges.min = -1.0f;
                parameter.ranges.max = 1.0f;
                parameter.ranges.def = 0.0f;
            }
            else
            {
                parameter.hints      = kParameterIsAutomatable|kParameterIsLogarithmic;
                parameter.unit       = "s";
                parameter.ranges.min = 0.001f;
                parameter.ranges.max = 10.0f;
                parameter.ranges.def = (g == 0) ? 0.001f : 0.3f;
            }
            return;
        }	

        if (index >= kEnvParam)
        {
            static const char* const names[] = { "attack", "decay", "sustain", "release" };

            const uint32_t g = (index - kEnvParam) / 16;
            const uint32_t s = (index - kEnvParam) % 16;
            if (g > 3) return;

            char name[40], sym[40];
            std::snprintf(name, sizeof(name), "slot%u-%s", s + 1, names[g]);
            std::snprintf(sym,  sizeof(sym),  "s%u_%s",    s + 1, names[g]);
            parameter.name   = name;
            parameter.symbol = sym;

            if (g == 2)
            {
                parameter.hints      = kParameterIsAutomatable;
                parameter.ranges.def = 1.0f;
            }
            else
            {
                parameter.hints      = kParameterIsAutomatable|kParameterIsLogarithmic;
                parameter.unit       = "s";
                parameter.ranges.min = 0.001f;
                parameter.ranges.max = 10.0f;
                parameter.ranges.def = (g == 0) ? 0.002f : (g == 1) ? 0.1f : 0.005f;
            }
            return;
        }	

        if (index >= kFilterParam)
        {
            const uint32_t g = (index - kFilterParam) / 16;
            const uint32_t s = (index - kFilterParam) % 16;
            if (g > 1) return;

            char name[40], sym[40];
            std::snprintf(name, sizeof(name), "slot%u-%s", s + 1, g == 0 ? "cutoff" : "resonance");
            std::snprintf(sym,  sizeof(sym),  "s%u_%s",    s + 1, g == 0 ? "cutoff" : "resonance");
            parameter.name   = name;
            parameter.symbol = sym;

            if (g == 0)
            {
                parameter.hints      = kParameterIsAutomatable|kParameterIsLogarithmic;
                parameter.unit       = "Hz";
                parameter.ranges.min = 20.0f;
                parameter.ranges.max = 20000.0f;
                parameter.ranges.def = 20000.0f;
            }
            else
            {
                parameter.hints = kParameterIsAutomatable; // 0..1, default 0
            }
            return;
        }	

        if (index >= kInterpParam)
        {
            char name[40], sym[40];
            std::snprintf(name, sizeof(name), "slot%u-interp", index - kInterpParam + 1);
            std::snprintf(sym,  sizeof(sym),  "s%u_interp",    index - kInterpParam + 1);
            parameter.name       = name;
            parameter.symbol     = sym;
            parameter.hints      = kParameterIsAutomatable|kParameterIsBoolean;
            parameter.ranges.def = 1.0f; // linear
            return;
        }	

        if (index >= kEndParam)
        {
            static const char* const names[] = { "end", "loop-start", "loop-end", "loop" };
            static const char* const syms[]  = { "end", "loop_start", "loop_end", "loop" };

            const uint32_t g = (index - kEndParam) / 16;
            const uint32_t s = (index - kEndParam) % 16;
            if (g > 3) return;

            char name[40], sym[40];
            std::snprintf(name, sizeof(name), "slot%u-%s", s + 1, names[g]);
            std::snprintf(sym,  sizeof(sym),  "s%u_%s",    s + 1, syms[g]);
            parameter.name   = name;
            parameter.symbol = sym;
            parameter.hints  = (g == 3) ? (kParameterIsAutomatable|kParameterIsBoolean)
                                        : kParameterIsAutomatable;
            parameter.ranges.def = (g == 1 || g == 3) ? 0.0f : 1.0f; // end/loop end = 1
            if (g == 0 || g == 2) parameter.ranges.def = 1.0f;
            return;
        }	

        if (index >= kBitParam)
        {
            char name[40], sym[40];
            std::snprintf(name, sizeof(name), "slot%u-bit8", index - kBitParam + 1);
            std::snprintf(sym,  sizeof(sym),  "s%u_bit8",    index - kBitParam + 1);
            parameter.name   = name;
            parameter.symbol = sym;
            parameter.hints  = kParameterIsAutomatable|kParameterIsBoolean;
            return; // range stays 0..1, default 0 (16-bit)
        }	

        if (index >= kGainParam)
        {
            char name[40], sym[40];
            std::snprintf(name, sizeof(name), "slot%u-gain", index - kGainParam + 1);
            std::snprintf(sym,  sizeof(sym),  "s%u_gain",    index - kGainParam + 1);
            parameter.name       = name;
            parameter.symbol     = sym;
            parameter.hints      = kParameterIsAutomatable;
            parameter.unit       = "dB";
            parameter.ranges.min = -24.0f;
            parameter.ranges.max = 24.0f;
            parameter.ranges.def = 0.0f;
            return;
        }	

        if (index >= 4)
        {
            static const char* const fieldNames[] = { "length", "root", "low", "high", "start" };

            const uint32_t s = (index - 4) / 5;
            const uint32_t f = (index - 4) % 5;

            char name[40], sym[40];
            std::snprintf(name, sizeof(name), "slot%u-%s", s + 1, fieldNames[f]);
            std::snprintf(sym,  sizeof(sym),  "s%u_%s",    s + 1, fieldNames[f]);
            parameter.name   = name;
            parameter.symbol = sym;

            switch (f)
            {
            case 0:
                parameter.hints      = kParameterIsOutput;
                parameter.unit       = "s";
                parameter.ranges.max = 3600.0f;
                break;
            case 1:
                parameter.hints      = kParameterIsAutomatable|kParameterIsInteger;
                parameter.ranges.max = 127.0f;
                parameter.ranges.def = 60.0f;
                break;
            case 2:
                parameter.hints      = kParameterIsAutomatable|kParameterIsInteger;
                parameter.ranges.max = 127.0f;
                break;
            case 3:
                parameter.hints      = kParameterIsAutomatable|kParameterIsInteger;
                parameter.ranges.max = 127.0f;
                parameter.ranges.def = 127.0f;
                break;
            case 4:
                parameter.hints      = kParameterIsAutomatable;
                break;
            }
            return;
        }	

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
        defaultValue = "";

        switch (index)
        {
        case 0: key = "name";   break;
        case 1: key = "folder"; break;
        case 2: key = "save";   break;
        default:
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "slot%u", index - 2); // indices 3..18 -> slot1..slot16
            key = buf;
            break;
        }
        }
    }  

   /* --------------------------------------------------------------------------------------------------------
    * Internal data */

   /**
      Get the current value of a parameter.
    */
    float getParameterValue(uint32_t index) const override
    {
        if (index == kLearnParam)
            return static_cast<float>(fLearnVal.load(std::memory_order_relaxed));

        if (index >= kPitchParam)
        {
            const uint32_t g = (index - kPitchParam) / 16;
            const uint32_t s = (index - kPitchParam) % 16;

            switch (g)
            {
            case 0: return fSlots[s].fine.load();
            case 1: return fSlots[s].detune.load();
            case 2: return fSlots[s].lfoRate.load();
            }
            return 0.0f;
        }	

        if (index >= kEnvParam)
        {
            const uint32_t g = (index - kEnvParam) / 16;
            const uint32_t s = (index - kEnvParam) % 16;

            switch (g)
            {
            case 0: return fSlots[s].attack.load();
            case 1: return fSlots[s].decay.load();
            case 2: return fSlots[s].sustain.load();
            case 3: return fSlots[s].release.load();
            }
            return 0.0f;
        }

        if (index >= kFEnvParam)
        {
            const uint32_t g = (index - kFEnvParam) / 16;
            const uint32_t s = (index - kFEnvParam) % 16;

            switch (g)
            {
            case 0: return fSlots[s].fenvAtt.load();
            case 1: return fSlots[s].fenvDec.load();
            case 2: return fSlots[s].fenvSus.load();
            case 3: return fSlots[s].fenvRel.load();
            case 4: return fSlots[s].fenvAmt.load();
            }
            return 0.0f;
        }	

        if (index >= kFilterParam)
        {
            const uint32_t g = (index - kFilterParam) / 16;
            const uint32_t s = (index - kFilterParam) % 16;
            return g == 0 ? fSlots[s].cutoff.load() : fSlots[s].res.load();
        }
	
        if (index >= kInterpParam)
        {
            const uint32_t s = index - kInterpParam;
            return (s < kNumSlots && fSlots[s].interp.load()) ? 1.0f : 0.0f;
        }	

	if (index >= kEndParam)
        {
            const uint32_t g = (index - kEndParam) / 16;
            const uint32_t s = (index - kEndParam) % 16;

            switch (g)
            {
            case 0: return fSlots[s].endPt.load();
            case 1: return fSlots[s].loopStart.load();
            case 2: return fSlots[s].loopEnd.load();
            case 3: return fSlots[s].loopOn.load() ? 1.0f : 0.0f;
            }
            return 0.0f;
        }
		
        if (index >= kBitParam)
        {
            const uint32_t s = index - kBitParam;
            return (s < kNumSlots && fSlots[s].bit8.load()) ? 1.0f : 0.0f;
        }	

        if (index >= kGainParam)
        {
            const uint32_t s = index - kGainParam;
            return s < kNumSlots ? fSlots[s].gainDb.load() : 0.0f;
        }	
      
        if (index >= 4)
        {
            const uint32_t s = (index - 4) / 5;
            if (s >= kNumSlots) return 0.0f;

            switch ((index - 4) % 5)
            {
            case 0: return fSlots[s].seconds.load();
            case 1: return static_cast<float>(fSlots[s].root.load());
            case 2: return static_cast<float>(fSlots[s].lo.load());
            case 3: return static_cast<float>(fSlots[s].hi.load());
            case 4: return fSlots[s].start.load();
            }
            return 0.0f;
        }
      
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

        if (index >= kPitchParam)
        {
            const uint32_t g = (index - kPitchParam) / 16;
            const uint32_t s = (index - kPitchParam) % 16;

            switch (g)
            {
            case 0: fSlots[s].fine.store(value);    break;
            case 1: fSlots[s].detune.store(value);  break;
            case 2: fSlots[s].lfoRate.store(value); break;
            }
            return;
        }      

        if (index >= kFEnvParam)
        {
            const uint32_t g = (index - kFEnvParam) / 16;
            const uint32_t s = (index - kFEnvParam) % 16;

            switch (g)
            {
            case 0: fSlots[s].fenvAtt.store(value); break;
            case 1: fSlots[s].fenvDec.store(value); break;
            case 2: fSlots[s].fenvSus.store(value); break;
            case 3: fSlots[s].fenvRel.store(value); break;
            case 4: fSlots[s].fenvAmt.store(value); break;
            }
            return;
        }      

        if (index >= kEnvParam)
        {
            const uint32_t g = (index - kEnvParam) / 16;
            const uint32_t s = (index - kEnvParam) % 16;

            switch (g)
            {
            case 0: fSlots[s].attack.store(value);  break;
            case 1: fSlots[s].decay.store(value);   break;
            case 2: fSlots[s].sustain.store(value); break;
            case 3: fSlots[s].release.store(value); break;
            }
            return;
        }      

        if (index >= kFilterParam)
        {
            const uint32_t g = (index - kFilterParam) / 16;
            const uint32_t s = (index - kFilterParam) % 16;

            if (g == 0) fSlots[s].cutoff.store(value);
            else        fSlots[s].res.store(value);
            return;
        }      

        if (index >= kInterpParam)
        {
            const uint32_t s = index - kInterpParam;
            if (s < kNumSlots)
                fSlots[s].interp.store(value > 0.5f);
            return;
        }      

        if (index >= kEndParam)
        {
            const uint32_t g = (index - kEndParam) / 16;
            const uint32_t s = (index - kEndParam) % 16;

            switch (g)
            {
            case 0: fSlots[s].endPt.store(value);          break;
            case 1: fSlots[s].loopStart.store(value);      break;
            case 2: fSlots[s].loopEnd.store(value);        break;
            case 3: fSlots[s].loopOn.store(value > 0.5f);  break;
            }
            return;
        }      

        if (index >= kBitParam)
        {
            const uint32_t s = index - kBitParam;
            if (s < kNumSlots)
                fSlots[s].bit8.store(value > 0.5f);
            return;
        }	

        if (index >= kGainParam)
        {
            const uint32_t s = index - kGainParam;
            if (s < kNumSlots)
            {
                fSlots[s].gainDb.store(value);
                fSlots[s].gainLin.store(std::pow(10.0f, value / 20.0f));
            }
            return;
        }
	
        if (index >= 4)
        {
            const uint32_t s = (index - 4) / 5;
            if (s >= kNumSlots) return;

            switch ((index - 4) % 5)
            {
            case 1: fSlots[s].root.store(static_cast<int>(value + 0.5f)); break;
            case 2: fSlots[s].lo.store(static_cast<int>(value + 0.5f));   break;
            case 3: fSlots[s].hi.store(static_cast<int>(value + 0.5f));   break;
            case 4: fSlots[s].start.store(value);                         break;
            }
            return;
        }
      
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
        else if (std::strncmp(key, "slot", 4) == 0)
        {
            const int n = std::atoi(key + 4);
            if (n >= 1 && n <= static_cast<int>(kNumSlots))
                loadSample(static_cast<uint32_t>(n - 1), value);
        }	
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

        // Try to take the sample lock; if the loader has it, skip samples this block.
        const bool haveLock = trySampleLock();

	        if (haveLock)
        {
            // slots that were just replaced: stop voices still pointing at the old data
            const uint32_t mask = fKillMask.exchange(0, std::memory_order_acq_rel);

            if (mask != 0)
            {
                for (SampleVoice& v : fVoices)
                    if (v.active && ((mask >> v.slot) & 1u))
                        v.active = false;

                fNote      = -1;
                fTargetAmp = 0.0f;
            }
        }

        uint32_t pos = 0;

        for (uint32_t i = 0; i < midiEventCount; ++i)
        {
            const uint32_t frame = midiEvents[i].frame < frames ? midiEvents[i].frame : frames;

            renderAudio(outputs, pos, frame, haveLock);
            handleNote(midiEvents[i], haveLock);
            pos = frame;
        }

        renderAudio(outputs, pos, frames, haveLock);

        if (haveLock)
            sampleUnlock();	
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

    // ---- Sample memory (guarded by fSampBusy) ----

    std::atomic<bool>  fSampBusy    { false };

    // ---- Voices (audio thread only) ----
    static constexpr uint32_t kNumVoices = 8;
    static constexpr int      kRootNote  = 60; // C4 plays at original pitch

      static constexpr uint32_t kNumSlots = 16;

    struct Slot
    {
        std::vector<float> L, R;       // guarded by fSampBusy
        uint32_t len  = 0;
        uint32_t rate = 44100;

        std::atomic<int>   root    { 60 };
        std::atomic<int>   lo      { 0 };
        std::atomic<int>   hi      { 127 };
        std::atomic<float> start   { 0.0f };
        std::atomic<float> seconds { 0.0f };
        std::atomic<float> gainDb  { 0.0f };
        std::atomic<float> gainLin { 1.0f };
      std::atomic<bool> bit8 {false};
        std::atomic<float> endPt     { 1.0f };
        std::atomic<float> loopStart { 0.0f };
        std::atomic<float> loopEnd   { 1.0f };
        std::atomic<bool>  loopOn    { false };
        std::atomic<bool> interp { true }; // true = linear, false = nearest sample
        std::atomic<float> cutoff { 20000.0f };
        std::atomic<float> res    { 0.0f };
        std::atomic<float> attack  { 0.002f };
        std::atomic<float> decay   { 0.1f };
        std::atomic<float> sustain { 1.0f };
        std::atomic<float> release { 0.005f };

        std::atomic<float> fenvAtt { 0.001f };
        std::atomic<float> fenvDec { 0.3f };
        std::atomic<float> fenvSus { 0.0f };
        std::atomic<float> fenvRel { 0.3f };
        std::atomic<float> fenvAmt { 0.0f };   // -1..+1, times 7 octaves
        std::atomic<float> fine    { 0.0f };   // semitones, -1..+1
        std::atomic<float> detune  { 0.0f };   // cents, 0..100
        std::atomic<float> lfoRate { 5.0f };   // Hz      
    };

    Slot fSlots[kNumSlots];

    std::atomic<bool>     fAnySample { false };
    std::atomic<uint32_t> fKillMask  { 0 };   // bit n = slot n was just replaced
  
    struct SampleVoice
    {
        bool   active    = false;
        bool   releasing = false;
        int    note      = -1;
      int slot = 0;
        double pos       = 0.0;
        double step      = 0.0;
        float  gain      = 0.0f;
        float  env       = 0.0f;
        float  ic1L = 0.0f, ic2L = 0.0f, ic1R = 0.0f, ic2R = 0.0f; // filter state
      int stage = 0; // 0 attack, 1 decay, 2 sustain - 'releasing' above sets release stage
        float  fenv   = 0.0f;
        int    fstage = 0;     // 0 attack, 1 decay, 2 sustain
        double pos2     = 0.0;   // detuned copy
        float  lfoPhase = 0.0f;  // 0..1      
    };

    SampleVoice fVoices[kNumVoices];
    uint32_t    fNextVoice = 0;

    static constexpr uint32_t kLearnParam = 84;
    static constexpr uint32_t kGainParam = 85; // 85..100 = slot gains
  static constexpr uint32_t kBitParam = 101; // 101..116 = slot 8-bit toggles
    static constexpr uint32_t kEndParam = 117; // 117..132 end, 133..148 loop start,
    static constexpr uint32_t kInterpParam = 181; // 181..196 = slot interpolation  
                                               // 149..164 loop end, 165..180 loop on
    static constexpr uint32_t kFilterParam = 197; // 197..212 cutoff, 213..228 resonance
    static constexpr uint32_t kEnvParam = 229; // 229 attack, 245 decay, 261 sustain, 277 release  
    static constexpr uint32_t kFEnvParam = 293; // attack, decay, sustain, release, amount (16 each)
    static constexpr uint32_t kPitchParam = 373; // fine, detune, lfo speed (16 each)
    static constexpr float    kLfoMaxSemis = 1.0f; // vibrato depth at full mod wheel

    float fModWheel  = 0.0f; // audio thread only
    float fModSmooth = 0.0f;  

    std::atomic<uint32_t> fLearnVal { 0 }; // (sequence * 128) + last note-on
    uint32_t fNoteSeq = 0;                 // audio thread only  

    bool trySampleLock() noexcept { return !fSampBusy.exchange(true, std::memory_order_acquire); }
    void sampleUnlock()  noexcept { fSampBusy.store(false, std::memory_order_release); }

    void lockSample() // loader thread only
    {
        while (fSampBusy.exchange(true, std::memory_order_acquire))
            std::this_thread::yield();
    }

    void renderAudio(float** outputs, uint32_t start, uint32_t end, bool haveLock) noexcept
    {
        renderSine(outputs, start, end);

        if (haveLock)
            renderSamples(outputs, start, end);
    }

    std::string samplesFolder() const
    {
        if (!fFolder.empty())
            return fFolder;

        const char* home = std::getenv("HOME");
        if (home == nullptr) home = std::getenv("USERPROFILE");
        return std::string(home ? home : ".") + "/Samples";
    }

    void startVoice(uint32_t slot, uint8_t note, uint8_t vel) noexcept
    {
        Slot& s = fSlots[slot];
        if (s.len < 2)
            return;

        SampleVoice* v = nullptr;

        for (SampleVoice& c : fVoices)
            if (!c.active) { v = &c; break; }

        const bool stolen = (v == nullptr);
        if (stolen)
        {
            v = &fVoices[fNextVoice];
            fNextVoice = (fNextVoice + 1) % kNumVoices;
        }

        v->active    = true;
        v->releasing = false;
        v->slot      = static_cast<int>(slot);
        v->note      = note;
        v->pos       = std::min(static_cast<double>(s.start.load(std::memory_order_relaxed))
                                    * static_cast<double>(s.len - 1),
                                static_cast<double>(s.len) - 2.0);
        v->step      = std::pow(2.0, (note - s.root.load(std::memory_order_relaxed)) / 12.0)
                     * static_cast<double>(s.rate) / getSampleRate();
        v->gain      = (vel / 127.0f) * 0.5f;
        if (!stolen)
            v->env   = 0.0f;
        v->ic1L = v->ic2L = v->ic1R = v->ic2R = 0.0f;
	v->stage = 0;
        v->fenv   = 0.0f;
        v->fstage = 0;
        v->pos2     = v->pos;
        v->lfoPhase = 0.0f;	
    }

    void renderSamples(float** outputs, uint32_t start, uint32_t end) noexcept
    {
        // smooth the mod wheel (once per segment) so depth changes don't zipper
        fModSmooth += (fModWheel - fModSmooth)
                    * std::min(1.0f, static_cast<float>(end - start) * 0.002f);

        const float sr = static_cast<float>(getSampleRate());

        for (SampleVoice& v : fVoices)
        {
            if (!v.active)
                continue;

            const Slot& s = fSlots[v.slot];

            if (s.len < 2)
            {
                v.active = false;
                continue;
            }

            const float* const L = s.L.data();
            const float* const R = s.R.data();
            const double lastFrame = static_cast<double>(s.len - 1);

            // playback end, and loop region (only used if it is at least 2 frames long)
            const double endPos = std::min(lastFrame, std::max(0.0,
                                  static_cast<double>(s.endPt.load(std::memory_order_relaxed)) * lastFrame));
            const double ls = static_cast<double>(s.loopStart.load(std::memory_order_relaxed)) * lastFrame;
            const double le = static_cast<double>(s.loopEnd.load(std::memory_order_relaxed))   * lastFrame;
            const bool looping = s.loopOn.load(std::memory_order_relaxed) && (le - ls) >= 2.0;
            const double loopLen = le - ls;

            const float slotGain = s.gainLin.load(std::memory_order_relaxed);

            const float qs = s.bit8.load(std::memory_order_relaxed) ? 128.0f : 32768.0f;
            auto quant = [qs](float x) noexcept
            {
                const float q = std::floor(x * qs + 0.5f);
                return std::min(qs - 1.0f, std::max(-qs, q)) / qs;
            };

            const bool interp = s.interp.load(std::memory_order_relaxed);

            // read one stereo frame at a fractional position (p must be < len - 1)
            auto fetch = [&](double p, float& l, float& r) noexcept
            {
                const uint32_t idx  = static_cast<uint32_t>(p);
                const float    frac = static_cast<float>(p - idx);

                if (interp)
                {
                    const float l0 = quant(L[idx]), l1 = quant(L[idx + 1]);
                    const float r0 = quant(R[idx]), r1 = quant(R[idx + 1]);

                    l = l0 + (l1 - l0) * frac;
                    r = r0 + (r1 - r0) * frac;
                }
                else
                {
                    l = quant(L[idx]);
                    r = quant(R[idx]);
                }
            };

            // ---- pitch: fine tune, detuned copy, LFO vibrato ----
            const float  fineSt   = s.fine.load(std::memory_order_relaxed);
            const float  detuneSt = s.detune.load(std::memory_order_relaxed) * 0.01f;
            const bool   dual     = detuneSt > 0.0005f;
            const float  dualGain = dual ? 0.707f : 1.0f;
            const float  lfoDepth = fModSmooth * kLfoMaxSemis;
            const bool   lfoOn    = lfoDepth > 0.0005f;
            const float  lfoInc   = s.lfoRate.load(std::memory_order_relaxed) / sr;

            double ratio  = std::exp2(static_cast<double>(fineSt) / 12.0);
            double ratio2 = std::exp2(static_cast<double>(fineSt + detuneSt) / 12.0);

            // ---- filter ----
            const float cutoff = s.cutoff.load(std::memory_order_relaxed);
            const float fAmt   = s.fenvAmt.load(std::memory_order_relaxed);
            const bool  fMod   = std::fabs(fAmt) > 0.001f;
            const bool  filt   = fMod || cutoff < 19999.0f;

            const float fK     = 1.4f - 1.35f * s.res.load(std::memory_order_relaxed);
            const float fMaxHz = 0.45f * sr;
            float fa1 = 0.0f, fa2 = 0.0f, fa3 = 0.0f;

            auto setCoef = [&](float hz) noexcept
            {
                const float fc = std::min(fMaxHz, std::max(20.0f, hz));
                const float g  = std::tan(3.14159265f * fc / sr);
                fa1 = 1.0f / (1.0f + g * (g + fK));
                fa2 = g * fa1;
                fa3 = g * fa2;
            };

            if (filt && !fMod)
                setCoef(cutoff);

            const float fSusLvl  = std::min(1.0f, std::max(0.0f, s.fenvSus.load(std::memory_order_relaxed)));
            const float fAttInc  = 1.0f / (std::max(0.001f, s.fenvAtt.load(std::memory_order_relaxed)) * sr);
            const float fDecDec  = (1.0f - fSusLvl) / (std::max(0.001f, s.fenvDec.load(std::memory_order_relaxed)) * sr);
            const float fRelCoef = std::exp(-6.9f / (std::max(0.001f, s.fenvRel.load(std::memory_order_relaxed)) * sr));
            int fCount = 0;

            // ---- amp envelope ----
            const float susLvl  = std::min(1.0f, std::max(0.0f, s.sustain.load(std::memory_order_relaxed)));
            const float attInc  = 1.0f / (std::max(0.001f, s.attack.load(std::memory_order_relaxed)) * sr);
            const float decDec  = (1.0f - susLvl) / (std::max(0.001f, s.decay.load(std::memory_order_relaxed)) * sr);
            const float relCoef = std::exp(-6.9f / (std::max(0.001f, s.release.load(std::memory_order_relaxed)) * sr));

            for (uint32_t i = start; i < end; ++i)
            {
                // amp envelope
                if (v.releasing)
                {
                    v.env *= relCoef;
                }
                else if (v.stage == 0)
                {
                    v.env += attInc;
                    if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; }
                }
                else if (v.stage == 1)
                {
                    v.env -= decDec;
                    if (v.env <= susLvl) { v.env = susLvl; v.stage = 2; }
                }
                else
                {
                    v.env = susLvl;
                }

                // wrap at the loop end; the loop end replaces the sample end while looping
                if (looping && v.pos >= le)
                    v.pos = ls + std::fmod(v.pos - ls, loopLen);

                if (looping && dual && v.pos2 >= le)
                    v.pos2 = ls + std::fmod(v.pos2 - ls, loopLen);

                if ((!looping && v.pos >= endPos)
                    || (v.releasing && v.env < 0.0005f)
                    || (v.stage == 2 && susLvl < 0.0005f))
                {
                    v.active = false;
                    break;
                }

                // LFO (sine); the phase always runs, the depth comes from the mod wheel
                if (lfoOn)
                {
                    const float m = std::sin(v.lfoPhase * 6.2831853f) * lfoDepth;
                    ratio  = std::exp2(static_cast<double>(fineSt + m) / 12.0);
                    ratio2 = std::exp2(static_cast<double>(fineSt + detuneSt + m) / 12.0);
                }
                v.lfoPhase += lfoInc;
                if (v.lfoPhase >= 1.0f)
                    v.lfoPhase -= 1.0f;

                // filter envelope
                if (fMod)
                {
                    if (v.releasing)
                    {
                        v.fenv *= fRelCoef;
                    }
                    else if (v.fstage == 0)
                    {
                        v.fenv += fAttInc;
                        if (v.fenv >= 1.0f) { v.fenv = 1.0f; v.fstage = 1; }
                    }
                    else if (v.fstage == 1)
                    {
                        v.fenv -= fDecDec;
                        if (v.fenv <= fSusLvl) { v.fenv = fSusLvl; v.fstage = 2; }
                    }
                    else
                    {
                        v.fenv = fSusLvl;
                    }

                    if ((fCount++ & 7) == 0)
                        setCoef(cutoff * std::exp2(fAmt * 7.0f * v.fenv));
                }

                float l, r;
                fetch(v.pos, l, r);

                if (dual)
                {
                    float l2 = 0.0f, r2 = 0.0f;

                    // the detuned copy may reach the sample end before the main one
                    if (looping || v.pos2 < endPos)
                        fetch(v.pos2, l2, r2);

                    l = (l + l2) * dualGain;
                    r = (r + r2) * dualGain;
                }

                if (filt)
                {
                    const float v3l = l - v.ic2L;
                    const float v1l = fa1 * v.ic1L + fa2 * v3l;
                    const float v2l = v.ic2L + fa2 * v.ic1L + fa3 * v3l;
                    v.ic1L = 2.0f * v1l - v.ic1L;
                    v.ic2L = 2.0f * v2l - v.ic2L;
                    l = v2l;

                    const float v3r = r - v.ic2R;
                    const float v1r = fa1 * v.ic1R + fa2 * v3r;
                    const float v2r = v.ic2R + fa2 * v.ic1R + fa3 * v3r;
                    v.ic1R = 2.0f * v1r - v.ic1R;
                    v.ic2R = 2.0f * v2r - v.ic2R;
                    r = v2r;
                }

                const float amp = v.env * v.gain * slotGain;
                outputs[0][i] += l * amp;
                outputs[1][i] += r * amp;

                v.pos += v.step * ratio;

                if (dual)
                    v.pos2 += v.step * ratio2;
                else
                    v.pos2 = v.pos; // stay in sync so detune can be switched on mid-note
            }
        }
    }  

    void handleNote(const MidiEvent& ev, bool haveLock) noexcept
    {
        if (ev.size < 3)
            return;

        const uint8_t status = ev.data[0] & 0xF0;
        const uint8_t note   = ev.data[1];
        const uint8_t vel    = ev.data[2];

        const bool noteOn  = status == 0x90 && vel > 0;
        const bool noteOff = status == 0x80 || (status == 0x90 && vel == 0);

        // mod wheel (CC1) sets the LFO depth
        if (status == 0xB0)
        {
            if (note == 1)
                fModWheel = vel / 127.0f;
            return;
        }	

        if (noteOn)
        {
            // publish the note for the UI's MIDI learn; the sequence number makes
            // repeated presses of the same note look like a change
            fNoteSeq = (fNoteSeq + 1) & 8191u;
            fLearnVal.store(fNoteSeq * 128u + note, std::memory_order_relaxed);
	  
            if (fAnySample.load(std::memory_order_relaxed))
            {
                // Sampler mode: only slots whose key range contains the note play.
                if (haveLock)
                {
                    for (uint32_t s = 0; s < kNumSlots; ++s)
                    {
                        if (fSlots[s].len >= 2
                            && note >= fSlots[s].lo.load(std::memory_order_relaxed)
                            && note <= fSlots[s].hi.load(std::memory_order_relaxed))
                            startVoice(s, note, vel);
                    }
                }
            }
            else
            {
                // nothing loaded: sine fallback
                fNote      = note;
                fFreq      = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
                fTargetAmp = (vel / 127.0f) * 0.25f;
            }
        }
        else if (noteOff)
        {
            for (SampleVoice& v : fVoices)
                if (v.active && v.note == note)
                    v.releasing = true;

            if (note == fNote)
            {
                fNote      = -1;
                fTargetAmp = 0.0f;
            }
        }
    }

    // Called from setState(), which is not the audio thread.
    void loadSample(uint32_t slot, const char* name)
    {
        const std::string n = name;

        if (slot >= kNumSlots || n.empty()
            || n.find('/')  != std::string::npos
            || n.find('\\') != std::string::npos
            || n.find("..") != std::string::npos)
            return;

        const std::string path = samplesFolder() + "/" + n;

        std::vector<float> L, R;
        uint32_t rate = 0;

        if (!readWav(path, L, R, rate))
        {
            std::printf("Could not load %s\n", path.c_str());
            return;
        }

        const uint32_t len = static_cast<uint32_t>(L.size());
        Slot& s = fSlots[slot];

        lockSample();
        s.L.swap(L);
        s.R.swap(R);
        s.len  = len;
        s.rate = rate;
        fKillMask.fetch_or(1u << slot, std::memory_order_release);
        sampleUnlock();
        // the old buffers are freed here, outside the lock

        s.seconds.store(static_cast<float>(len) / static_cast<float>(rate));
        fAnySample.store(true, std::memory_order_relaxed);

        std::printf("Slot %u: loaded %s (%u frames @ %u Hz)\n", slot + 1, path.c_str(), len, rate);
    }  

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

	const std::string folder = samplesFolder();

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
