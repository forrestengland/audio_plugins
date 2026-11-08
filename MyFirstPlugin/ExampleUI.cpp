
#include "DistrhoUI.hpp"
#include "MidiMonitor.hpp"
#include "WaveReader.hpp"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <cstdlib>
#include <vector>
#include <dirent.h>

START_NAMESPACE_DISTRHO

#define PLUGIN_TITLE "MALVERN"

using DGL_NAMESPACE::Color;

static const float kWaveTop = 252.0f;
static const float kWaveH = 80.0f;
static const uint32_t kPeakBins = 1024;
static const float kListTop = 372.0f; // y of the first row
static const float kRowH    = 18.0f;

static const float kColW     = 400.0f;  // left column width; the right column starts here
static const float kSlotX    = 420.0f;
static const float kSlotW    = 360.0f;
static const float kSlotTop  = 52.0f;
static const float kSlotRowH = 22.0f;
static const float kEditTop  = 440.0f;
static const float kMinusX   = 520.0f;
static const float kPlusX    = 700.0f;

static const float    kLearnX     = 736.0f;
static const uint32_t kLearnParam = 84;

static const uint32_t kGainParam = 85;

static float editRowY(int k) { return kEditTop + 30.0f + k * 34.0f; }
static uint32_t slotParam(int slot, int field) { return 4u + slot * 5u + field; }

class MyFirstUI : public UI
{
public:
  MyFirstUI()
    : UI(800, 694),
      fMidiMonitor(nullptr),
      fEventCount(0)
  {
    if (!loadSharedResources())
      std::printf("Failed to load DPF shared resources\n");

#if !DISTRHO_PLUGIN_TARGET_LV2
    Plugin* const plugin = static_cast<Plugin*>(getPluginInstancePointer());
    fMidiMonitor = dynamic_cast<MidiMonitorPlugin*>(plugin);
#endif

    scanSamples();
  }

protected:

      bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        if (!ev.press)
        {
            // end of a start-point drag
            if (fDraggingStart)
            {
                fDraggingStart = false;
                editParameter(slotParam(fSel, 4), false);
                return true;
            }
            return false;
        }      

        const double x = ev.pos.getX();
        const double y = ev.pos.getY();

        // Text field: click inside to edit, click anywhere else to stop editing.
        const double fy = getHeight() - 148.0;
        const bool inField = x >= 70 && x <= kColW - 20
                          && y >= fy && y <= fy + 24.0;

        if (inField)
        {
            fEditing = true;
            repaint();
            return true;
        }

        if (fEditing)
        {
            fEditing = false;
            repaint();
        }

        // Right column: slot list and key-range editor.
        if (x >= kSlotX && x <= kSlotX + kSlotW)
        {
            if (y >= kSlotTop && y < kSlotTop + 16 * kSlotRowH)
            {
                selectSlot(static_cast<int>((y - kSlotTop) / kSlotRowH));
                return true;
            }

            const int step = (ev.mod & kModifierShift) ? 12 : 1;

            for (int k = 0; k < 3; ++k)
            {
                const double ry = editRowY(k);
                if (y < ry || y > ry + 24.0)
                    continue;

                if (x >= kMinusX && x <= kMinusX + 26.0) { adjustSlot(k + 1, -step); return true; }
                if (x >= kPlusX  && x <= kPlusX  + 26.0) { adjustSlot(k + 1,  step); return true; }
		if (x >= kLearnX && x <= kLearnX + 44.0f)
                {
                    fLearn = (fLearn == k + 1) ? -1 : k + 1; // click again to cancel
                    repaint();
                    return true;
                }
            }

            const double gy = editRowY(3);
            if (y >= gy && y <= gy + 24.0)
            {
                const float gstep = (ev.mod & kModifierShift) ? 6.0f : 1.0f;

                if (x >= kMinusX && x <= kMinusX + 26.0)
                    adjustGain(-gstep);
                else if (x >= kPlusX && x <= kPlusX + 26.0)
                    adjustGain(gstep);
                else if (x > kMinusX + 26.0 && x < kPlusX)
                    adjustGain(-fSlot[fSel].gain); // reset to 0 dB

                return true;
            }	    
	    
            return true;
        }	

	// Waveform: click or drag to set the start point.
        if (!fPeakMin.empty() && x >= 20 && x <= kColW - 20
                              && y >= kWaveTop && y <= kWaveTop + kWaveH)
        {
            fDraggingStart = true;
            editParameter(slotParam(fSel, 4), true); // tells the host a gesture started
            setStartFromX(x);
            return true;
        }

        // File list: click to select a row.
        if (x >= 20 && x <= kColW - 20 && y >= kListTop && y < listBottom())
        {
            const int row = static_cast<int>((y - kListTop) / kRowH);
            const int idx = fFirstRow + row;

            if (row < visibleRows() && idx < static_cast<int>(fFiles.size()))
            {
                fSelected = fFiles[idx];
                repaint();
            }
            return true;
        }	

        // Buttons.
        const double by = getHeight() - 108.0;

        if (y < by || y > by + 24.0)
            return false;

        if (x >= 20 && x <= 120)
        {
            fRecording = !fRecording;
            setParameterValue(3, fRecording ? 1.0f : 0.0f);
            repaint();
            return true;
        }

        if (x >= 130 && x <= 230)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%u", ++fSaveCount);
            setState("save", buf);
            return true;
        }

	if (x >= 240 && x <= 340)
        {
            if (!fSelected.empty() && loadPeaks(fSelected))
            {
                fSlot[fSel].name = fSelected;
                setState(("slot" + std::to_string(fSel + 1)).c_str(), fSelected.c_str());
            }
            return true;
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!fDraggingStart)
            return false;

        setStartFromX(ev.pos.getX());
        return true;
    }  

    bool onScroll(const ScrollEvent& ev) override
    {
        const double x = ev.pos.getX();
        const double y = ev.pos.getY();

        if (x >= kSlotX && x <= kSlotX + kSlotW)
        {
            for (int k = 0; k < 3; ++k)
            {
                if (y >= editRowY(k) && y <= editRowY(k) + 24.0)
                {
                    fWheelAcc += ev.delta.getY();
                    const int steps = static_cast<int>(fWheelAcc);

                    if (steps != 0)
                    {
                        fWheelAcc -= static_cast<float>(steps);
                        adjustSlot(k + 1, steps);
                    }
                    return true;
                }
            }

            if (y >= editRowY(3) && y <= editRowY(3) + 24.0)
            {
                fWheelAcc += ev.delta.getY();
                const int steps = static_cast<int>(fWheelAcc);

                if (steps != 0)
                {
                    fWheelAcc -= static_cast<float>(steps);
                    adjustGain(static_cast<float>(steps));
                }
                return true;
            }	    
            return true;
        }	

        if (x < 20 || x > getWidth() - 20 || y < kListTop || y > listBottom())
            return false;

        // Accumulate so smooth trackpad deltas and wheel notches both behave.
        fScrollAcc -= ev.delta.getY() * 2.0f; // rows per wheel notch
        const int steps = static_cast<int>(fScrollAcc);

        if (steps != 0)
        {
            fScrollAcc -= static_cast<float>(steps);
            fFirstRow += steps;
            clampScroll();
            repaint();
        }

        return true;
    }  

      bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!fEditing)
            return false;

        // Special keys arrive here; printable characters arrive in onCharacterInput.
        // 0x08 = backspace, 0x1B = escape (DPF's kKeyBackspace / kKeyEscape).
        if (ev.press)
        {
            if (ev.key == 0x08)
            {
                if (!fName.empty())
                {
                    fName.pop_back();
                    commitName();
                }
            }
            else if (ev.key == 0x1B || ev.key == '\r' || ev.key == '\n')
            {
                fEditing = false;
                repaint();
            }
        }

        return ev.key == 0x08 || ev.key == 0x1B || ev.key == '\r' || ev.key == '\n';
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (!fEditing)
            return false;

        const uint32_t c = ev.character;

        // Same character set the plugin's sanitize() allows.
        const bool allowed = c < 128
                          && (std::isalnum(static_cast<int>(c)) || c == '-' || c == '_' || c == ' ');

        if (allowed && fName.size() < 32)
        {
            fName += static_cast<char>(c);
            commitName();
        }

        return true; // swallow everything while editing
    }
  
    void uiIdle() override
    {

      // Rescan the samples folder roughly once a second.
        if (++fScanTick >= 60)
        {
            fScanTick = 0;
            scanSamples();
        }
      
        if (fMidiMonitor == nullptr)
            return;

        MidiMonitorEvent event;
        bool changed = false;

        while (fMidiMonitor->popMidiEvent(event))
        {
            // Newest event goes at the top.
            for (uint32_t i = 7; i > 0; --i)
                fEvents[i] = fEvents[i - 1];

            fEvents[0] = event;

            if (fEventCount < 8)
                ++fEventCount;

            changed = true;
        }

        if (changed)
            repaint();
    }

    void drawSlots()
    {
        fontSize(14.0f);
        fillColor(Color(100, 220, 140));
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(kSlotX, 30.0f, "SAMPLE SLOTS", nullptr);

        beginPath();
        roundedRect(kSlotX, kSlotTop, kSlotW, kSlotRowH * 16, 4.0f);
        fillColor(Color(40, 40, 46));
        fill();

        fontSize(12.0f);

        for (int i = 0; i < 16; ++i)
        {
            const SlotInfo& s = fSlot[i];
            const float ry  = kSlotTop + i * kSlotRowH;
            const float mid = ry + kSlotRowH * 0.5f;

            if (i == fSel)
            {
                beginPath();
                rect(kSlotX + 2.0f, ry + 1.0f, kSlotW - 4.0f, kSlotRowH - 2.0f);
                fillColor(Color(55, 105, 75));
                fill();
            }

            char num[8];
            std::snprintf(num, sizeof(num), "%02d", i + 1);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(Color(140, 140, 150));
            text(kSlotX + 8.0f, mid, num, nullptr);

            if (s.name.empty())
            {
                fillColor(Color(100, 100, 110));
                text(kSlotX + 34.0f, mid, "(empty)", nullptr);
                continue;
            }

            std::string shown = s.name;
            if (shown.size() > 28)
                shown = shown.substr(0, 26) + "..";

            fillColor(Color(220, 220, 225));
            text(kSlotX + 34.0f, mid, shown.c_str(), nullptr);

            char lo[8], hi[8], range[24];
            noteLabel(s.lo, lo, sizeof(lo));
            noteLabel(s.hi, hi, sizeof(hi));
            std::snprintf(range, sizeof(range), "%s-%s", lo, hi);

            fillColor(Color(140, 140, 150));
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(kSlotX + kSlotW - 8.0f, mid, range, nullptr);
        }

        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawEditor()
    {
        static const char* const labels[] = { "ROOT NOTE", "KEY LOW", "KEY HIGH" };

        const SlotInfo& s = fSlot[fSel];
        const int vals[3] = { s.root, s.lo, s.hi };

        char buf[48];
        std::snprintf(buf, sizeof(buf), "SLOT %d SETTINGS", fSel + 1);

        fontSize(14.0f);
        fillColor(Color(100, 220, 140));
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(kSlotX, kEditTop, buf, nullptr);

        for (int k = 0; k < 3; ++k)
        {
            const float y = editRowY(k);

            fontSize(12.0f);
            fillColor(Color(220, 220, 225));
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(kSlotX, y + 12.0f, labels[k], nullptr);

            drawButton("-", kMinusX, y, 26.0f, 24.0f, Color(70, 70, 80));
            drawButton("+", kPlusX,  y, 26.0f, 24.0f, Color(70, 70, 80));

            const bool armed = (fLearn == k + 1);
            drawButton(armed ? "WAIT" : "LEARN", kLearnX, y, 44.0f, 24.0f,
                       armed ? Color(200, 150, 40) : Color(70, 70, 80));	    

            char nb[8];
            noteLabel(vals[k], nb, sizeof(nb));
            std::snprintf(buf, sizeof(buf), "%s  (%d)", nb, vals[k]);

            fontSize(13.0f);
            fillColor(Color(255, 255, 255));
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text((kMinusX + 26.0f + kPlusX) * 0.5f, y + 12.0f, buf, nullptr);
        }

        {
            const float gy = editRowY(3);

            fontSize(12.0f);
            fillColor(Color(220, 220, 225));
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(kSlotX, gy + 12.0f, "GAIN", nullptr);

            drawButton("-", kMinusX, gy, 26.0f, 24.0f, Color(70, 70, 80));
            drawButton("+", kPlusX,  gy, 26.0f, 24.0f, Color(70, 70, 80));

            std::snprintf(buf, sizeof(buf), "%+.1f dB", s.gain);
            fontSize(13.0f);
            fillColor(Color(255, 255, 255));
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text((kMinusX + 26.0f + kPlusX) * 0.5f, gy + 12.0f, buf, nullptr);
        }

        fontSize(11.0f);
        fillColor(Color(140, 140, 150));
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(kSlotX, editRowY(3) + 44.0f,
             "Click +/-: 1 step (Shift: octave / 6 dB)   Wheel: 1 step   Click gain value: reset", nullptr);	

    }  

    void onNanoDisplay() override
    {
        const float width = kColW;
        const float height = getHeight();

        // Background.
        beginPath();
        rect(0, 0, getWidth(), height);
        fillColor(30, 30, 35, 255);
        fill();

        const FontId font = findFont(NANOVG_DEJAVU_SANS_TTF);

        if (font < 0)
            return;

        fontFaceId(font);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);

        // Main title.
        fontSize(20.0f);
        fillColor(Color(255, 255, 255));

        text(20, 25, PLUGIN_TITLE, nullptr);

        // Monitor heading.
        fontSize(14.0f);
        fillColor(Color(100, 220, 140));

        text(20, 57, "MIDI MONITOR", nullptr);

        // Divider.
        beginPath();
        moveTo(20, 70);
        lineTo(width - 20, 70);
        strokeColor(Color(75, 75, 85));
        strokeWidth(1.0f);
        stroke();

        // Event list.
        fontSize(13.0f);
        fillColor(Color(220, 220, 225));

        if (fEventCount == 0)
        {
            text(20, 92, "Waiting for MIDI...", nullptr);
        }
        else
        {
            char line[80];

            for (uint32_t i = 0; i < fEventCount; ++i)
            {
                const uint8_t status = fEvents[i].status;
                const uint8_t type = status & 0xF0;
                const uint8_t channel = (status & 0x0F) + 1;

                const uint8_t note = fEvents[i].data1;
                const uint8_t velocity = fEvents[i].data2;

                const char* noteNames[] = {
                    "C", "C#", "D", "D#", "E", "F",
                    "F#", "G", "G#", "A", "A#", "B"
                };

                const bool noteOff =
                    type == 0x80 || (type == 0x90 && velocity == 0);

                std::snprintf(
                    line,
                    sizeof(line),
                    "%-7s CH %u   %-2s%d   VEL %u",
                    noteOff ? "NOTE OFF" : "NOTE ON",
                    channel,
                    noteNames[note % 12],
                    (note / 12) - 1,
                    velocity
                );

                const float y = 86.0f + i * 18.0f;
                text(20, y, line, nullptr);
            }
        }

	drawSlots();
	drawEditor();

	drawWaveform(width);
	drawFileList(width);

        drawTextField(20, height - 148.0f, width - 40.0f, 24.0f);	

        const float by = height - 108.0f;
        drawButton(fRecording ? "STOP" : "REC", 20, by, 100, 24,
                   fRecording ? Color(200, 60, 60) : Color(70, 70, 80));
        drawButton("SAVE", 130, by, 100, 24, Color(70, 70, 80));

	char loadLabel[16];
        std::snprintf(loadLabel, sizeof(loadLabel), "LOAD > %d", fSel + 1);
        drawButton(loadLabel, 240, by, 100, 24,
                   fSelected.empty() ? Color(55, 55, 62) : Color(70, 70, 80));

	// Level meters, anchored to the bottom of the window.
        fontSize(12.0f);
        fillColor(Color(100, 220, 140));
        text(20, height - 72, "INPUT LEVEL", nullptr);

        drawMeter("L", 20, height - 58, width - 40, 16, fLevelL);
        drawMeter("R", 20, height - 34, width - 40, 16, fLevelR);
    }

    void parameterChanged(uint32_t index, float value) override
    {

      if (index == kLearnParam)
        {
            handleLearnValue(static_cast<int>(value + 0.5f));
            repaint();
            return;
        }

        if (index >= kGainParam)
        {
            if (index - kGainParam < 16)
                fSlot[index - kGainParam].gain = value;
            repaint();
            return;
        }      
	      
        if (index >= 4)
        {
            const uint32_t s = (index - 4) / 5;

            if (s < 16)
            {
                SlotInfo& si = fSlot[s];

                switch ((index - 4) % 5)
                {
                case 0: si.sec   = value; break;
                case 1: si.root  = static_cast<int>(value + 0.5f); break;
                case 2: si.lo    = static_cast<int>(value + 0.5f); break;
                case 3: si.hi    = static_cast<int>(value + 0.5f); break;
                case 4: si.start = value; break;
                }
            }
        }
        else
        {
            switch (index)
            {
            case 0: fColor = value; break;
            case 1: fLevelL = value; break;
            case 2: fLevelR = value; break;
            case 3: fRecording = value > 0.5f; break;
            }
        }

        repaint();
    }  

      void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "name") == 0 && !fEditing)
        {
            fName = value;
            repaint();
        }
        else if (std::strcmp(key, "folder") == 0)
        {
            fFolder = value;
            scanSamples();
	}
        else if (std::strncmp(key, "slot", 4) == 0)
        {
            const int n = std::atoi(key + 4);

            if (n >= 1 && n <= 16)
            {
                fSlot[n - 1].name = value;
                if (n - 1 == fSel)
                    loadPeaks(value);
            }
        }	    
    }

private:
    MidiMonitorAccess* fMidiMonitor;

    MidiMonitorEvent fEvents[8];
    uint32_t fEventCount;
    float fColor  = 0.0f;
    float fLevelL = 0.0f;
    float fLevelR = 0.0f;

    bool fRecording = false;
    uint32_t fSaveCount = 0;

    std::string fName { "sample" };
    bool fEditing = false;

    std::vector<std::string> fFiles;
    std::string fSelected;
    std::string fFolder;
    int   fFirstRow  = 0;
    float fScrollAcc = 0.0f;
    int   fScanTick  = 0;

    std::vector<float> fPeakMin, fPeakMax;
    bool  fDraggingStart = false;

      struct SlotInfo
    {
        std::string name;
        int   root = 60, lo = 0, hi = 127;
        float sec = 0.0f, start = 0.0f;
      float gain = 0.0f;
    };

    SlotInfo fSlot[16];
    int      fSel      = 0;
    float    fWheelAcc = 0.0f;

    int fLearn        = -1;  // -1 = off, else the field being learned (1 root, 2 low, 3 high)
    int fLastLearnVal = -1;

    void handleLearnValue(int v)
    {
        if (v == fLastLearnVal)
            return;
        fLastLearnVal = v;

        if (fLearn < 0)
            return;

        const SlotInfo& s = fSlot[fSel];
        const int cur  = (fLearn == 1) ? s.root : (fLearn == 2) ? s.lo : s.hi;
        const int note = v % 128;
        const int field = fLearn;

        fLearn = -1;
        adjustSlot(field, note - cur); // reuses the clamping and low <= high logic
    }  

    static void noteLabel(int n, char* buf, size_t size)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };
        std::snprintf(buf, size, "%s%d", names[n % 12], n / 12 - 1);
    }

    void selectSlot(int i)
    {
      fLearn = -1;
        fSel = i;
        fPeakMin.clear();
        fPeakMax.clear();

        if (!fSlot[i].name.empty())
            loadPeaks(fSlot[i].name);

        repaint();
    }

    void adjustGain(float delta)
    {
        float& g = fSlot[fSel].gain;
        g = std::min(24.0f, std::max(-24.0f, g + delta));
        setParameterValue(kGainParam + fSel, g);
        repaint();
    }  

    // field: 1 = root, 2 = key low, 3 = key high
    void adjustSlot(int field, int delta)
    {
        SlotInfo& s = fSlot[fSel];
        int& v = (field == 1) ? s.root : (field == 2) ? s.lo : s.hi;

        v = std::min(127, std::max(0, v + delta));
        setParameterValue(slotParam(fSel, field), static_cast<float>(v));

        // keep low <= high by pushing the other end along
        if (field == 2 && s.lo > s.hi)
        {
            s.hi = s.lo;
            setParameterValue(slotParam(fSel, 3), static_cast<float>(s.hi));
        }
        else if (field == 3 && s.hi < s.lo)
        {
            s.lo = s.hi;
            setParameterValue(slotParam(fSel, 2), static_cast<float>(s.lo));
        }

        repaint();
    }

  void setStartFromX(double x)
    {
        const float f = (static_cast<float>(x) - 20.0f) / (kColW - 40.0f);
        fSlot[fSel].start = std::min(1.0f, std::max(0.0f, f));
        setParameterValue(slotParam(fSel, 4), fSlot[fSel].start);
        repaint();
    }

    // Reads the loaded file and reduces it to min/max pairs for drawing.
    bool loadPeaks(const std::string& name)
    {
        if (name.empty() || name.find('/')  != std::string::npos
                         || name.find('\\') != std::string::npos
                         || name.find("..") != std::string::npos)
            return false;

        std::vector<float> L, R;
        uint32_t rate = 0;

        if (!readWav(samplesFolder() + "/" + name, L, R, rate))
            return false;

        const size_t len  = L.size();
        const size_t bins = std::min<size_t>(kPeakBins, len);

        std::vector<float> pmin(bins, 0.0f), pmax(bins, 0.0f);

        for (size_t b = 0; b < bins; ++b)
        {
            const size_t s0 = b * len / bins;
            size_t       s1 = (b + 1) * len / bins;
            if (s1 <= s0) s1 = s0 + 1;

            float lo = L[s0], hi = L[s0];
            for (size_t i = s0; i < s1; ++i)
            {
                lo = std::min({lo, L[i], R[i]});
                hi = std::max({hi, L[i], R[i]});
            }
            pmin[b] = lo;
            pmax[b] = hi;
        }

        fPeakMin.swap(pmin);
        fPeakMax.swap(pmax);
        repaint();
        return true;
    }  

    void drawWaveform(float width)
    {
        const float x = 20.0f;
        const float w = width - 40.0f;
        const float y = kWaveTop;
        const float h = kWaveH;

        // Header: title on the left, start time on the right.
        fontSize(14.0f);
        fillColor(Color(100, 220, 140));
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x, y - 14.0f, "WAVEFORM", nullptr);

        if (!fPeakMin.empty())
        {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "START %.3f s", fSlot[fSel].start * fSlot[fSel].sec);

            fontSize(12.0f);
            fillColor(Color(140, 140, 150));
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(x + w, y - 14.0f, buf, nullptr);
        }

        // Panel.
        beginPath();
        roundedRect(x, y, w, h, 4.0f);
        fillColor(Color(40, 40, 46));
        fill();

        if (fPeakMin.empty())
        {
            fontSize(13.0f);
            fillColor(Color(140, 140, 150));
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(x + w * 0.5f, y + h * 0.5f, "Load a sample to see its waveform", nullptr);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            return;
        }

        const float mid = y + h * 0.5f;
        const float amp = h * 0.5f - 4.0f;

        // Center line.
        beginPath();
        moveTo(x, mid);
        lineTo(x + w, mid);
        strokeColor(Color(70, 70, 80));
        strokeWidth(1.0f);
        stroke();

        // One vertical line per pixel column, spanning that column's min..max.
        const int    cols = std::max(1, static_cast<int>(w));
        const size_t bins = fPeakMin.size();

        beginPath();
        for (int c = 0; c < cols; ++c)
        {
            const size_t b0 = static_cast<size_t>(c) * bins / cols;
            size_t       b1 = static_cast<size_t>(c + 1) * bins / cols;
            if (b1 <= b0)   b1 = b0 + 1;
            if (b1 > bins)  b1 = bins;

            float lo = fPeakMin[b0], hi = fPeakMax[b0];
            for (size_t b = b0; b < b1; ++b)
            {
                lo = std::min(lo, fPeakMin[b]);
                hi = std::max(hi, fPeakMax[b]);
            }
            lo = std::max(-1.0f, lo);
            hi = std::min( 1.0f, hi);

            const float px     = x + c + 0.5f;
            const float top    = mid - hi * amp;
            const float bottom = std::max(mid - lo * amp, top + 1.0f);

            moveTo(px, top);
            lineTo(px, bottom);
        }
        strokeColor(Color(100, 220, 140));
        strokeWidth(1.0f);
        stroke();

        // Dim the part before the start point, since it will be skipped.
        const float sx = x + w * fSlot[fSel].start;

        beginPath();
        rect(x, y, sx - x, h);
        fillColor(0, 0, 0, 120);
        fill();

        // Start marker with a small handle at the top.
        beginPath();
        moveTo(sx, y);
        lineTo(sx, y + h);
        strokeColor(Color(255, 200, 80));
        strokeWidth(2.0f);
        stroke();

        beginPath();
        moveTo(sx, y);
        lineTo(sx + 9.0f, y);
        lineTo(sx, y + 9.0f);
        closePath();
        fillColor(Color(255, 200, 80));
        fill();
    }  

    float listBottom() const { return getHeight() - 160.0f; }

    int visibleRows() const
    {
        return std::max(1, static_cast<int>((listBottom() - kListTop) / kRowH));
    }

    void clampScroll()
    {
        const int maxFirst = std::max(0, static_cast<int>(fFiles.size()) - visibleRows());
        fFirstRow = std::min(std::max(fFirstRow, 0), maxFirst);
    }

    // Same default as the plugin's saveRecording().
    std::string samplesFolder() const
    {
        if (!fFolder.empty())
            return fFolder;

        const char* home = std::getenv("HOME");
        if (home == nullptr) home = std::getenv("USERPROFILE");
        return std::string(home ? home : ".") + "/Samples";
    }

    static bool hasWavExtension(const std::string& n)
    {
        if (n.size() < 5)
            return false;

        std::string ext = n.substr(n.size() - 4);
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        return ext == ".wav";
    }

    void scanSamples()
    {
        std::vector<std::string> found;

        if (DIR* const dir = opendir(samplesFolder().c_str()))
        {
            while (const dirent* const e = readdir(dir))
            {
                const std::string n = e->d_name;
                if (hasWavExtension(n))
                    found.push_back(n);
            }
            closedir(dir);
        }

        std::sort(found.begin(), found.end());

        if (found != fFiles)
        {
            fFiles.swap(found);
            clampScroll();
            repaint();
        }
    }

    void drawFileList(float width)
    {
        clampScroll(); // the window may have been resized

        const float x = 20.0f;
        const float w = width - 40.0f;
        const float h = listBottom() - kListTop;
        const int   rows = visibleRows();
        const int   count = static_cast<int>(fFiles.size());

        // Header and file count.
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d files", count);

        fontSize(14.0f);
        fillColor(Color(100, 220, 140));
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x, kListTop - 18.0f, "SAMPLES", nullptr);

        fontSize(12.0f);
        fillColor(Color(140, 140, 150));
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(x + w, kListTop - 18.0f, buf, nullptr);

        char mem[40];
        if (fSlot[fSel].sec > 0.0f)
            std::snprintf(mem, sizeof(mem), "SLOT %d  %.2f s", fSel + 1, fSlot[fSel].sec);
        else
            std::snprintf(mem, sizeof(mem), "SLOT %d  empty", fSel + 1);

        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 90.0f, kListTop - 18.0f, mem, nullptr);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 90.0f, kListTop - 18.0f, mem, nullptr);	

        // Panel.
        beginPath();
        roundedRect(x, kListTop, w, h, 4.0f);
        fillColor(Color(40, 40, 46));
        fill();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);

        if (count == 0)
        {
            fillColor(Color(140, 140, 150));
            text(x + 8.0f, kListTop + kRowH * 0.5f, "No .wav files found", nullptr);
            return;
        }

        // Rows.
        for (int i = 0; i < rows; ++i)
        {
            const int idx = fFirstRow + i;
            if (idx >= count)
                break;

            const float ry = kListTop + i * kRowH;

            if (fFiles[idx] == fSelected)
            {
                beginPath();
                rect(x + 2.0f, ry, w - 12.0f, kRowH);
                fillColor(Color(55, 105, 75));
                fill();
            }

            fillColor(Color(220, 220, 225));
            text(x + 8.0f, ry + kRowH * 0.5f, fFiles[idx].c_str(), nullptr);
        }

        // Scrollbar thumb, only when the list overflows.
        if (count > rows)
        {
            const float thumbH = std::max(12.0f, h * rows / count);
            const float travel = h - thumbH;
            const float thumbY = kListTop + travel * fFirstRow / (count - rows);

            beginPath();
            roundedRect(x + w - 7.0f, thumbY, 4.0f, thumbH, 2.0f);
            fillColor(Color(110, 110, 125));
            fill();
        }
    }  

    void commitName()
    {
        setState("name", fName.c_str());
        repaint();
    }

    void drawTextField(float x, float y, float w, float h)
    {
        const float labelW = 50.0f;
        const float fx = x + labelW;
        const float fw = w - labelW;

        fontSize(12.0f);
        fillColor(Color(220, 220, 225));
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x, y + h * 0.5f, "NAME", nullptr);

        beginPath();
        roundedRect(fx, y, fw, h, 4.0f);
        fillColor(fEditing ? Color(60, 60, 72) : Color(45, 45, 52));
        fill();

        if (fEditing)
        {
            beginPath();
            roundedRect(fx, y, fw, h, 4.0f);
            strokeColor(Color(100, 220, 140));
            strokeWidth(1.0f);
            stroke();
        }

        // A trailing underscore stands in for the caret while editing.
        const std::string shown = fEditing ? fName + "_" : fName;

        fontSize(13.0f);
        fillColor(Color(255, 255, 255));
        text(fx + 8.0f, y + h * 0.5f, shown.c_str(), nullptr);
    }  

    void drawButton(const char* label, float x, float y, float w, float h, Color c)
    {
        beginPath();
        roundedRect(x, y, w, h, 4.0f);
        fillColor(c);
        fill();

        fontSize(12.0f);
        fillColor(Color(255, 255, 255));
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(x + w * 0.5f, y + h * 0.5f, label, nullptr);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
    }  

    // Map linear level to 0..1 across a -60 dB .. 0 dB range.
    static float levelToFraction(float level)
    {
        if (level <= 0.001f)
            return 0.0f;

        const float db = 20.0f * std::log10(level);
        return std::min(1.0f, std::max(0.0f, (db + 60.0f) / 60.0f));
    }

    void drawMeter(const char* label, float x, float y, float w, float h, float level)
    {
        const float labelW = 16.0f;
        const float barX   = x + labelW;
        const float barW   = w - labelW;
        const float frac   = levelToFraction(level);

        // Label.
        fontSize(12.0f);
        fillColor(Color(220, 220, 225));
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x, y + h * 0.5f, label, nullptr);

        // Track.
        beginPath();
        roundedRect(barX, y, barW, h, 3.0f);
        fillColor(Color(50, 50, 58));
        fill();

        if (frac <= 0.0f)
            return;

        // Fill color: parameter color normally, red when near clipping (> -1 dB).
        Color c = (fColor < 0.5f) ? Color(70, 210, 120) : Color(80, 150, 240);
        if (level > 0.89f)
            c = Color(235, 70, 70);

        beginPath();
        roundedRect(barX, y, barW * frac, h, 3.0f);
        fillColor(c);
        fill();
    }  

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyFirstUI)
};

UI* createUI()
{
    return new MyFirstUI();
}

END_NAMESPACE_DISTRHO
