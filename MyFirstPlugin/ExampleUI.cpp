
#include "DistrhoUI.hpp"
#include "MidiMonitor.hpp"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

class MyFirstUI : public UI
{
public:
  MyFirstUI()
    : UI(400, 390),
      fMidiMonitor(nullptr),
      fEventCount(0)
  {
    if (!loadSharedResources())
      std::printf("Failed to load DPF shared resources\n");

#if !DISTRHO_PLUGIN_TARGET_LV2
    Plugin* const plugin = static_cast<Plugin*>(getPluginInstancePointer());
    fMidiMonitor = dynamic_cast<MidiMonitorPlugin*>(plugin);
#endif    
  }

protected:

      bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || !ev.press)
            return false;

        const double x = ev.pos.getX();
        const double y = ev.pos.getY();

        // Text field: click inside to edit, click anywhere else to stop editing.
        const double fy = getHeight() - 148.0;
        const bool inField = x >= 70 && x <= getWidth() - 20
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

        return false;
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

    void onNanoDisplay() override
    {
        const float width = getWidth();
        const float height = getHeight();

        // Background.
        beginPath();
        rect(0, 0, width, height);
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

        text(20, 25, "MY FIRST PLUGIN", nullptr);

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

        drawTextField(20, height - 148.0f, width - 40.0f, 24.0f);	

        const float by = height - 108.0f;
        drawButton(fRecording ? "STOP" : "REC", 20, by, 100, 24,
                   fRecording ? Color(200, 60, 60) : Color(70, 70, 80));
        drawButton("SAVE", 130, by, 100, 24, Color(70, 70, 80));	

	// Level meters, anchored to the bottom of the window.
        fontSize(12.0f);
        fillColor(Color(100, 220, 140));
        text(20, height - 72, "INPUT LEVEL", nullptr);

        drawMeter("L", 20, height - 58, width - 40, 16, fLevelL);
        drawMeter("R", 20, height - 34, width - 40, 16, fLevelR);
    }

      void parameterChanged(uint32_t index, float value) override
    {
        switch (index)
        {
        case 0: fColor = value; break;
        case 1: fLevelL = value; break;
        case 2: fLevelR = value; break;
	case 3: fRecording = value > 0.5f; break;
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
