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
        : MidiMonitorPlugin(6, 0, 4),
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
        case 4:
            parameter.hints  = kParameterIsOutput;
            parameter.name   = "sample-length";
            parameter.symbol = "sample_length";
            parameter.unit   = "s";
            parameter.ranges.max = 3600.0f;
            break;
        case 5:
            parameter.hints  = kParameterIsAutomatable;
            parameter.name   = "sample-start";
            parameter.symbol = "sample_start";
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
	case 3: key = "load"; defaultValue = ""; break;
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
	case 4: return fSampSeconds.load();
	case 5: return fStart.load();
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
	case 5: fStart.store(value); break;
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
	else if (std::strcmp(key, "load") == 0)
	  loadSample(value);
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

        if (haveLock && fKillVoices.exchange(false, std::memory_order_acq_rel))
        {
            // a new sample was swapped in: stop voices that pointed at the old one
            for (SampleVoice& v : fVoices)
                v.active = false;

            fNote      = -1;
            fTargetAmp = 0.0f;
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
    std::vector<float> fSampL, fSampR;
    uint32_t fSampLen  = 0;
    uint32_t fSampRate = 44100;

    std::atomic<bool>  fSampBusy    { false };
    std::atomic<bool>  fHasSample   { false };
    std::atomic<bool>  fKillVoices  { false };
    std::atomic<float> fSampSeconds { 0.0f };

    // ---- Voices (audio thread only) ----
    static constexpr uint32_t kNumVoices = 8;
    static constexpr int      kRootNote  = 60; // C4 plays at original pitch

  std::atomic<float> fStart {0.0f};

    struct SampleVoice
    {
        bool   active    = false;
        bool   releasing = false;
        int    note      = -1;
        double pos       = 0.0;
        double step      = 0.0;
        float  gain      = 0.0f;
        float  env       = 0.0f;
    };

    SampleVoice fVoices[kNumVoices];
    uint32_t    fNextVoice = 0;

    bool trySampleLock() noexcept { return !fSampBusy.exchange(true, std::memory_order_acquire); }
    void sampleUnlock()  noexcept { fSampBusy.store(false, std::memory_order_release); }

    void lockSample() // loader thread only
    {
        while (fSampBusy.exchange(true, std::memory_order_acquire))
            std::this_thread::yield();
    }

    void startVoice(uint8_t note, uint8_t vel) noexcept
    {
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
        v->note      = note;
        v->pos       = std::min(static_cast<double>(fStart.load(std::memory_order_relaxed)) * static_cast<double>(fSampLen - 1), static_cast<double>(fSampLen) - 2.0);
        v->step      = std::pow(2.0, (note - kRootNote) / 12.0)
                     * static_cast<double>(fSampRate) / getSampleRate();
        v->gain      = (vel / 127.0f) * 0.5f;
        if (!stolen)
            v->env   = 0.0f; // a stolen voice keeps its envelope to avoid a click
    }

    void renderSamples(float** outputs, uint32_t start, uint32_t end) noexcept
    {
        if (fSampLen < 2)
            return;

        const float* const L = fSampL.data();
        const float* const R = fSampR.data();
        const double lastPos = static_cast<double>(fSampLen - 1);

        for (SampleVoice& v : fVoices)
        {
            if (!v.active)
                continue;

            const float target = v.releasing ? 0.0f : 1.0f;

            for (uint32_t i = start; i < end; ++i)
            {
                v.env += (target - v.env) * 0.01f;

                if (v.pos >= lastPos || (v.releasing && v.env < 0.0005f))
                {
                    v.active = false;
                    break;
                }

                const uint32_t idx  = static_cast<uint32_t>(v.pos);
                const float    frac = static_cast<float>(v.pos - idx);
                const float    amp  = v.env * v.gain;

                // linear interpolation
                outputs[0][i] += (L[idx] + (L[idx + 1] - L[idx]) * frac) * amp;
                outputs[1][i] += (R[idx] + (R[idx + 1] - R[idx]) * frac) * amp;

                v.pos += v.step;
            }
        }
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

    // Called from setState(), which is not the audio thread.
    void loadSample(const char* name)
    {
        const std::string n = name;

        // ignore empty names (default state replay) and anything that tries to leave the folder
        if (n.empty() || n.find('/') != std::string::npos
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

        lockSample();
        fSampL.swap(L);
        fSampR.swap(R);
        fSampLen  = len;
        fSampRate = rate;
        fHasSample.store(true, std::memory_order_relaxed);
        fKillVoices.store(true, std::memory_order_release);
        sampleUnlock();
        // the old buffers are freed here, outside the lock

        fSampSeconds.store(static_cast<float>(len) / static_cast<float>(rate));
        std::printf("Loaded %s (%u frames @ %u Hz)\n", path.c_str(), len, rate);
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

    void handleNote(const MidiEvent& ev, bool haveLock) noexcept
    {
        if (ev.size < 3)
            return;

        const uint8_t status = ev.data[0] & 0xF0;
        const uint8_t note   = ev.data[1];
        const uint8_t vel    = ev.data[2];

        const bool noteOn  = status == 0x90 && vel > 0;
        const bool noteOff = status == 0x80 || (status == 0x90 && vel == 0);

        if (noteOn)
        {
            if (fHasSample.load(std::memory_order_relaxed))
            {
                if (haveLock)
                    startVoice(note, vel);
            }
            else
            {
                // no sample loaded: sine fallback
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
