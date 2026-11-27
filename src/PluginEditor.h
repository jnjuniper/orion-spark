#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace orion
{

// Expensive artwork rendered once at the display's physical pixel scale. While
// the window is being resized the old image is stretched, and a fresh one is
// rendered shortly after resizing stops.
class CachedArt
{
public:
    using Renderer = std::function<void (juce::Graphics&, juce::Rectangle<float> area, float scale)>;

    explicit CachedArt (juce::Component& ownerComponent) : owner (ownerComponent) {}

    void draw (juce::Graphics& g, juce::Rectangle<float> area, const Renderer& render);
    void invalidate() { image = {}; }

private:
    juce::Component& owner;
    juce::Image image;
    bool renderNow = false, pending = false;
};

// Photoreal knob (shadow, rendered body, pointer) driven by a Slider.
class RealKnob final : public juce::Slider
{
public:
    explicit RealKnob (float bodyDiameter);
    void paint (juce::Graphics&) override;

private:
    float diameter;
    CachedArt body { *this };
};

// Stomp switch; toggles the pedal's "on" parameter.
class FootSwitch final : public juce::Button
{
public:
    FootSwitch();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    CachedArt up { *this }, pressed { *this };
};

// Small chrome bat-lever toggle.
class MiniToggle final : public juce::Button
{
public:
    MiniToggle();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class PedalPanel final : public juce::Component, private juce::Timer
{
public:
    PedalPanel (juce::AudioProcessorValueTreeState& state, const Pedal& pedal);
    ~PedalPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Points (in this component) where the cables plug in.
    juce::Point<float> getInputJack() const;
    juce::Point<float> getOutputJack() const;

private:
    struct Control
    {
        std::unique_ptr<RealKnob> knob;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String label;
        juce::Point<float> centre;
    };

    struct Switch
    {
        std::unique_ptr<MiniToggle> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
        juce::String onLabel, offLabel;
        juce::Point<float> centre;
    };

    void timerCallback() override;
    void renderFace (juce::Graphics&, juce::Rectangle<float> area, float scale) const;
    void drawMotif (juce::Graphics&, juce::Rectangle<float> area) const;

    juce::String id, title, subtitle;
    std::atomic<float>* onParam = nullptr;
    bool ledOn = false;

    std::vector<Control> controls;
    std::vector<Switch> switches;
    FootSwitch footswitch;
    juce::AudioProcessorValueTreeState::ButtonAttachment footswitchAttachment;

    CachedArt face { *this }, ledLit { *this }, ledDark { *this };
};

// Round push button in the header that switches the tuner on and off.
class TunerButton final : public juce::Button
{
public:
    TunerButton();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// Takes the preset display's place while the tuner is on: the note, a row of LEDs
// from 50 cents flat to 50 cents sharp, and the offset in cents. Click it to close.
class TunerDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit TunerDisplay (const OrionAudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

    std::function<void()> onDismiss;

    static constexpr float inTuneCents = 2.0f;

private:
    void timerCallback() override;

    const OrionAudioProcessor& processor;
    PitchDetector detector;
    double preparedRate = 0.0;
    std::vector<float> input;

    // What's on the glass. A new note must be heard twice in a row before it replaces
    // the shown one, so the display doesn't flicker between neighbours; the last note
    // stays up briefly after the string goes quiet.
    NoteReading shown;
    int candidateNote = -1, candidateFrames = 0;
    double lastPitchTime = 0.0;
};

// Preset selector styled as a backlit display, with prev/next buttons.
class PresetDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit PresetDisplay (OrionAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class StepButton final : public juce::Button
    {
    public:
        explicit StepButton (bool isNext);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        bool next;
    };

    void timerCallback() override;
    void refreshUserPresets();
    int getCurrentIndex() const;          // position in factory + user presets, or -1
    void loadIndex (int index);
    void step (int delta);
    void showMenu();
    juce::Rectangle<float> getWindowArea() const;

    void saveCurrent();
    void saveAs (const juce::String& name);
    void renameCurrent (const juce::String& newName);
    void deleteCurrent();

    // Confirmations open inside the plugin window (a separate window can end up behind
    // the host's); the name dialog is its own always-on-top window so it can take
    // typing. All are owned here, so closing the editor also closes them.
    void askForName (const juce::String& title, const juce::String& initialName, const juce::String& confirmText,
                     std::function<void (const juce::String&)> onConfirm);
    void confirm (const juce::String& title, const juce::String& message, const juce::String& confirmText,
                  std::function<void()> onConfirm);
    void showMessage (const juce::String& title, const juce::String& message);
    juce::Component& getDialogParent();

    OrionAudioProcessor& processor;
    StepButton previous { false }, next { true };
    std::vector<juce::String> userPresets;   // cached; refreshed when the library changes
    juce::uint32 shownChangeCount = 0;
    bool shownModified = false;

    std::unique_ptr<juce::AlertWindow> nameDialog;
    juce::ScopedMessageBox messageBox;
};

class OrionLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    OrionLookAndFeel();
    juce::Font getSliderPopupFont (juce::Slider&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};

// Everything at the design size; the editor scales it to the window size.
class EditorRoot final : public juce::Component
{
public:
    explicit EditorRoot (OrionAudioProcessor&);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 1290;
    static constexpr int designHeight = 530;

private:
    void renderHeader (juce::Graphics&, juce::Rectangle<float> area, float scale) const;
    void renderBoard (juce::Graphics&, juce::Rectangle<float> area, float scale) const;
    void renderNameplate (juce::Graphics&, juce::Rectangle<float> plate) const;
    void setTunerShown (bool);

    OrionAudioProcessor& processor;
    PresetDisplay presetDisplay;
    RealKnob inputKnob { 40.0f }, outputKnob { 40.0f };
    juce::AudioProcessorValueTreeState::SliderAttachment inputAttachment, outputAttachment;
    std::vector<std::unique_ptr<PedalPanel>> panels;
    TunerButton tunerButton;
    TunerDisplay tunerDisplay { processor };

    CachedArt header { *this }, boardArt { *this };
};

} // namespace orion

class OrionAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit OrionAudioProcessorEditor (OrionAudioProcessor&);
    ~OrionAudioProcessorEditor() override;

    void resized() override;

private:
    orion::OrionLookAndFeel lookAndFeel;
    orion::EditorRoot root;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrionAudioProcessorEditor)
};
