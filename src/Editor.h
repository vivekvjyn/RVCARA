#pragma once

#include "Processor.h"

#include "ara/DocumentController.h"
#include "ui/OverviewStrip.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/PitchCurveView.h"
#include "ui/PropertyPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvcara
{
/** @brief The window: a header carrying the wordmark, the tools and the history, the piano roll
           and its property panel below that, and the take at a glance along the bottom.
*/
class Editor final : public juce::AudioProcessorEditor,
                     public juce::AudioProcessorEditorARAExtension,
                     private juce::Timer,
                     private DocumentController::Listener
{
public:
    /** @param processorToUse  Named to avoid shadowing AudioProcessorEditor::processor. */
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

    /** @brief Returns true when ARA is both bound and actually rendering this instance. */
    [[nodiscard]] bool isRenderingThroughARA() const;

    [[nodiscard]] Report describeState() const;
    [[nodiscard]] Report describeModification() const;
    [[nodiscard]] Report describeCapture() const;
    [[nodiscard]] ConversionModification* getFocusedModification() const;
    [[nodiscard]] juce::String describeLoadedVoice() const;
    [[nodiscard]] juce::String describeVoiceDetail() const;

    /** @brief What the panel says about note detection for the region on show. */
    [[nodiscard]] static juce::String describeNotes (const ConversionModification& modification);

    void applyPitchEdit (const PitchEdit& edit);

    void refresh();
    void showVoiceMenu();
    void showScaleMenu();
    void applyVoice (const juce::String& name);
    void applyScale (int root, int mode);
    void chooseTool (PitchTrack::Tool tool);

    void paintHeader (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;

    Processor& processorReference;

    PanelLookAndFeel lookAndFeel;

    juce::TextButton selectButton { "Select" };
    juce::TextButton splitButton { "Split" };
    juce::TextButton glueButton { "Glue" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };

    PitchCurveView pitchCurveView;
    PropertyPanel propertyPanel;
    OverviewStrip overviewStrip;

    Report report;

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> toolCapsule;

    int scaleRoot { 0 };
    int scaleMode { 0 };

    /** @brief The region the editor is showing, so an edit lands on what the user was looking at. */
    ConversionModification* shownModification { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace rvcara
