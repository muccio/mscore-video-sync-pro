#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace MScoreVideoSync
{

namespace Vep8Theme
{
    // Studio Dark Palette
    static const juce::Colour bgMain        = juce::Colour::fromRGB(18, 20, 23);   // #121417
    static const juce::Colour bgPanel       = juce::Colour::fromRGB(27, 31, 36);   // #1B1F24
    static const juce::Colour bgInput       = juce::Colour::fromRGB(34, 39, 46);   // #22272E
    static const juce::Colour bgHover       = juce::Colour::fromRGB(45, 52, 62);   // #2D343E
    static const juce::Colour border        = juce::Colour::fromRGB(45, 51, 59);   // #2D333B
    static const juce::Colour borderLight   = juce::Colour::fromRGB(65, 72, 83);   // #414853

    // Accents
    static const juce::Colour cyan          = juce::Colour::fromRGB(0, 210, 255);  // #00D2FF Playhead / Sync
    static const juce::Colour amber         = juce::Colour::fromRGB(255, 176, 0);  // #FFB000 SMPTE / Warning
    static const juce::Colour red           = juce::Colour::fromRGB(255, 59, 48);  // #FF3B30 Hit / Punch
    static const juce::Colour green         = juce::Colour::fromRGB(0, 230, 130);  // #00E682 Active Lock
    static const juce::Colour purple        = juce::Colour::fromRGB(175, 82, 222); // #AF52DE Cues

    // Typography
    static const juce::Colour textPrimary   = juce::Colour::fromRGB(230, 237, 243);
    static const juce::Colour textSecondary = juce::Colour::fromRGB(139, 148, 158);
    static const juce::Colour textDim       = juce::Colour::fromRGB(72, 79, 88);

    inline juce::Font getMonospaceFont(float height, bool bold = true)
    {
        return juce::Font(juce::FontOptions("Monaco", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font getUiFont(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    }
}

class Vep8LookAndFeel : public juce::LookAndFeel_V4
{
public:
    Vep8LookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, Vep8Theme::bgMain);
        setColour(juce::TextButton::buttonColourId, Vep8Theme::bgPanel);
        setColour(juce::TextButton::buttonOnColourId, Vep8Theme::cyan.withAlpha(0.25f));
        setColour(juce::TextButton::textColourOffId, Vep8Theme::textPrimary);
        setColour(juce::TextButton::textColourOnId, Vep8Theme::cyan);

        setColour(juce::ComboBox::backgroundColourId, Vep8Theme::bgInput);
        setColour(juce::ComboBox::outlineColourId, Vep8Theme::border);
        setColour(juce::ComboBox::textColourId, Vep8Theme::textPrimary);
        setColour(juce::ComboBox::arrowColourId, Vep8Theme::cyan);

        setColour(juce::Slider::backgroundColourId, Vep8Theme::bgInput);
        setColour(juce::Slider::thumbColourId, Vep8Theme::cyan);
        setColour(juce::Slider::trackColourId, Vep8Theme::border);

        setColour(juce::PopupMenu::backgroundColourId, Vep8Theme::bgPanel);
        setColour(juce::PopupMenu::textColourId, Vep8Theme::textPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Vep8Theme::cyan.withAlpha(0.2f));
        setColour(juce::PopupMenu::highlightedTextColourId, Vep8Theme::cyan);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        juce::Colour bg = backgroundColour;

        if (shouldDrawButtonAsDown)
            bg = bg.darker(0.15f);
        else if (shouldDrawButtonAsHighlighted)
            bg = bg.brighter(0.12f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(button.getToggleState() ? Vep8Theme::cyan : Vep8Theme::border);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        juce::Path p;
        float arrowX = static_cast<float>(buttonX + buttonW / 2);
        float arrowY = static_cast<float>(buttonY + buttonH / 2);
        p.addTriangle(arrowX - 4.0f, arrowY - 2.0f,
                      arrowX + 4.0f, arrowY - 2.0f,
                      arrowX, arrowY + 3.0f);

        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.fillPath(p);
    }
};

} // namespace MScoreVideoSync
