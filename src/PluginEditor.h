#pragma once

#include "PluginProcessor.h"

#include "DocumentController.h"
#include "PanelLookAndFeel.h"
#include "PitchCurveView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace rvcara
{

/** The plug-in's interface: a header, a display, and a footer.

    There are no knobs. The conversion has parameters and the host can automate every one of
    them, but putting them on the panel would misrepresent what using this plug-in is like:
    the voice loads itself, the take converts itself, and the one question the user actually
    has — "did the model hear the notes I sang?" — is answered by the pitch curve, not by a
    control. So the curve gets the whole middle of the panel, the header says which voice is
    loaded, and the footer says what the conversion is doing.

    The editor is a view onto the model, never a store of its own. It polls on a timer rather
    than subscribing to every change, because renders publish from a worker thread and several
    instances may share one document controller; at interface rate, polling is both simpler
    and indistinguishable from the alternative.
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

    /** What the panel is currently reporting: one line of status, and how to colour it. */
    struct Report
    {
        juce::String status;                  ///< Two or three words for the footer
        juce::String detail;                  ///< Right-aligned context, or empty
        juce::String caption;                 ///< Longer text drawn over the display, or empty
        float progress { 0.0f };              ///< Drives the footer's progress rule
        bool isBusy { false };                ///< Converting or queued
        bool isAlert { false };               ///< Failed
    };

    /** @returns What to show, read from whichever of the two paths is in use. */
    [[nodiscard]] Report describeState() const;

    /** The ARA half of describeState. */
    [[nodiscard]] Report describeModification() const;

    /** The insert-mode half of describeState, for hosts with no ARA. */
    [[nodiscard]] Report describeCapture() const;

    /** Reads state out of the model and into the panel. */
    void refresh();

    /** Pops the list of installed voices, and loads the one chosen. */
    void showVoiceMenu();

    /** Requests a voice by name through whichever path is in use. */
    void applyVoice (const juce::String& name);

    /** @returns The modification the interface is showing, or nullptr. */
    [[nodiscard]] ConversionModification* getFocusedModification() const;

    /** @returns The loaded voice's name and sample rate, for the header. */
    [[nodiscard]] juce::String describeLoadedVoice() const;

    void paintHeader (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintFooter (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;

    PluginProcessor& processorReference;

    PanelLookAndFeel lookAndFeel;

    /** The voice name, drawn as a field that opens a menu — the preset-field idiom, rather
        than a combo box, because there is normally exactly one voice and a box implies a
        decision the user does not have to make.
    */
    juce::TextButton voiceButton;

    juce::TextButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    /** Insert mode only: converts what has been captured without waiting for the transport to
        stop, and throws the capture away. Hidden under ARA, where the host hands over the
        audio and there is nothing to capture.
    */
    juce::TextButton convertButton { "Convert" };
    juce::TextButton clearCaptureButton { "Clear" };

    PitchCurveView pitchCurveView;

    /** The report the last refresh produced, so paint() and the display agree. */
    Report report;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace rvcara
