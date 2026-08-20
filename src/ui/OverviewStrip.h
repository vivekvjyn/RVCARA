#pragma once

#include "common/ConversionResult.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace rvcara
{
/** @brief The whole take at a glance, under the roll: its loudness as a silhouette, a mark per
           note, and a box around what the roll is showing. Dragging the box scrolls the roll.
*/
class OverviewStrip final : public juce::Component
{
public:
    OverviewStrip();

    void setConversion (ConversionPointer conversion);
    void setPitchEdit (PitchEdit edit);

    /** @brief Says which stretch of the take the roll is showing, so the box can follow it. */
    void setVisibleRange (double startSeconds, double endSeconds);

    /** @brief Called with the time the user wants in the middle of the roll. */
    std::function<void (double centreSeconds)> onScrubbed;

    void paint (juce::Graphics& graphics) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;

private:
    static constexpr int resolution = 1024;

    void rebuild();

    [[nodiscard]] double getSeconds() const;
    [[nodiscard]] float getXForSeconds (double seconds) const;
    void scrubTo (float x);

    ConversionPointer conversion;
    PitchEdit edit;
    std::vector<float> envelope;

    double visibleStart { 0.0 };
    double visibleEnd { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverviewStrip)
};

} // namespace rvcara
