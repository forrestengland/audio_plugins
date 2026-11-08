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
        : MidiMonitorPlugin(101, 0, 19),
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
    };

    SampleVoice fVoices[kNumVoices];
    uint32_t    fNextVoice = 0;

    static constexpr uint32_t kLearnParam = 84;
    static constexpr uint32_t kGainParam = 85; // 85..100 = slot gains  

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
    }

    void renderSamples(float** outputs, uint32_t start, uint32_t end) noexcept
    {
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
            const double lastPos = static_cast<double>(s.len - 1);
            const float target   = v.releasing ? 0.0f : 1.0f;
            const float slotGain = s.gainLin.load(std::memory_order_relaxed);

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
                const float    amp  = v.env * v.gain * slotGain;

                outputs[0][i] += (L[idx] + (L[idx + 1] - L[idx]) * frac) * amp;
                outputs[1][i] += (R[idx] + (R[idx + 1] - R[idx]) * frac) * amp;

                v.pos += v.step;
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
