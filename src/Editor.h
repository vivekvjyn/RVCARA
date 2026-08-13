#pragma once

#include "Processor.h"

#include "ara/DocumentController.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/PitchCurveView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace rvcara
{
/** @brief The panel: a header naming the voice, the pitch display, and a footer reporting state. */
class Editor final : public juce::AudioProcessorEditor,
                           public juce::AudioProcessorEditorARAExtension,
                           private juce::Timer,
                           private DocumentController::Listener
{
public:
    explicit Editor (Processor& processorToUse);
    ~Editor() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void conversionStateChanged() override;

    /** @brief What the panel is currently reporting. */
    struct Report
    {
        juce::String status;
        juce::String detail;
        juce::String caption;
        float progress { 0.0f };
        bool isBusy { false };
        bool isAlert { false };
    };

    [[nodiscard]] Report describeState() const;

    [[nodiscard]] Report describeModification() const;

    [[nodiscard]] Report describeCapture() const;

    void refresh();

    void showVoiceMenu();

    void applyVoice (const juce::String& name);

    [[nodiscard]] ConversionModification* getFocusedModification() const;

    [[nodiscard]] juce::String describeLoadedVoice() const;

    void paintHeader (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintFooter (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;

    Processor& processorReference;

    PanelLookAndFeel lookAndFeel;

    juce::TextButton voiceButton;

    juce::TextButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::TextButton convertButton { "Convert" };
    juce::TextButton clearCaptureButton { "Clear" };

    PitchCurveView pitchCurveView;

    Report report;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};
}
