
#include "DistrhoUI.hpp"
#include "MidiMonitor.hpp"

#include <cstdio>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

class MyFirstUI : public UI
{
public:
  MyFirstUI()
    : UI(400, 240),
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
    }

    void parameterChanged(uint32_t, float) override {}
    void stateChanged(const char*, const char*) override {}

private:
    MidiMonitorAccess* fMidiMonitor;

    MidiMonitorEvent fEvents[8];
    uint32_t fEventCount;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyFirstUI)
};

UI* createUI()
{
    return new MyFirstUI();
}

END_NAMESPACE_DISTRHO
