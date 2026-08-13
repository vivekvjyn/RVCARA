#pragma once

#include "PluginProcessor.h"

#include "DocumentController.h"
#include "PitchCurveView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace rvcara
{

/** The plugin's interface.

    Laid out around the one question the user is actually asking — "does this sound like
    the voice I chose, singing what I sang?" — so the pitch curve gets the space and the
    controls sit beneath it.

    The editor is a view onto the ARA model, not a store of its own. It polls rather than
    subscribing to every model change because a render publishes from a worker thread and
    several plugin instances may share a document controller; a timer at interface rate is
    both simpler and sufficient for progress, state and staleness.
*/
class PluginEditor final : public juce::AudioProcessorEditor,
                           public juce::AudioProcessorEditorARAExtension,
                           private juce::Timer,
                           private DocumentController::Listener
{
public:
    /** @param processorToUse  Named to avoid shadowing AudioProcessorEditor::processor,
                               which is the same object typed as the base class.
    */
    explicit PluginEditor (PluginProcessor& processorToUse);
    ~PluginEditor() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void conversionStateChanged() override;

    /** Repopulates the voice list from the library, preserving the current selection. */
    void refreshVoiceList();

    /** Pushes the combo box's selection into the ARA model. */
    void applySelectedVoice();

    /** Reads state back out of the model into the controls and the curve. */
    void refreshFromModel();

    /** The insert-mode half of refreshFromModel, for hosts with no ARA. */
    void refreshFromInsertConverter();

    /** @returns The modification the interface is currently showing, or nullptr. */
    [[nodiscard]] ConversionModification* getFocusedModification() const;

    /** One labelled control. */
    struct LabelledSlider
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    /** Builds and attaches one control. */
    void addSlider (LabelledSlider& control, const char* parameterId, const juce::String& name);

    PluginProcessor& processorReference;

    juce::Label titleLabel;
    juce::Label statusLabel;

    juce::ComboBox voiceSelector;
    juce::Label voiceLabel;
    juce::TextButton rescanButton { "Rescan" };

    PitchCurveView pitchCurveView;

    LabelledSlider pitchControl;
    LabelledSlider timbreControl;
    LabelledSlider consonantControl;
    LabelledSlider dynamicsControl;
    LabelledSlider variationControl;

    /** Insert mode only: converts what has been captured, without waiting for the transport
        to stop. In ARA mode the host supplies the audio directly, so there is nothing to
        capture and the button is hidden.
    */
    juce::TextButton convertButton { "Convert" };
    juce::TextButton clearCaptureButton { "Clear" };

    juce::ToggleButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    /** The voices offered, parallel to the combo box's items. */
    std::vector<juce::String> voiceNames;

    /** Set when the editor is writing to the controls, to stop the write echoing back. */
    bool isRefreshing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace rvcara
