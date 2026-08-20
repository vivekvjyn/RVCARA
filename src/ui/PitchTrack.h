#pragma once

#include "ara/ConversionModification.h"

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvcara
{
/** @brief The pitch editor's grid: one row per semitone, a block per note the segmenter found,
           the melody the voice sang drawn over them and the take's own melody behind, with the
           converted waveform on the middle C line. Its size follows the zoom, so the viewport
           scrolls it.

    Dragging a block moves the note relative to what was sung rather than onto an absolute
    pitch, which is what lets an untouched note come back out of the vocoder unchanged.
*/
class PitchTrack final : public juce::Component
{
public:
    PitchTrack();

    static constexpr int lowestNote = 36;
    static constexpr int highestNote = 84;
    static constexpr int numRows = highestNote - lowestNote + 1;

    /** @brief The row the waveform is drawn on, which is middle C. */
    static constexpr int waveformNote = 60;

    static constexpr float minimumPixelsPerSecond = 16.0f;
    static constexpr float maximumPixelsPerSecond = 320.0f;
    static constexpr float minimumRowHeight = 3.0f;
    static constexpr float maximumRowHeight = 26.0f;

    /** @brief What a click does. */
    enum class Tool
    {
        select,
        split,
        glue
    };

    /** @brief The controls drawn above a note that is on its own in the selection. */
    enum class Handle
    {
        none,
        tiltLeft,
        shape,
        tiltRight
    };

    /** @brief Shows a conversion, or clears the grid when given nullptr. */
    void setConversion (ConversionPointer conversion);

    /** @brief Shows a note list, unless the user is in the middle of dragging one. */
    void setPitchEdit (PitchEdit newEdit);

    [[nodiscard]] const PitchEdit& getPitchEdit() const noexcept { return edit; }

    void setTool (Tool newTool);
    [[nodiscard]] Tool getTool() const noexcept { return tool; }

    /** @brief Called with the new note list whenever the user finishes changing it. */
    std::function<void (const PitchEdit&)> onEditChanged;

    /** @brief Called when the selection changes, so the panel can follow it. */
    std::function<void()> onSelectionChanged;

    /** @brief Asks the panel to zoom, since the viewport rather than the grid owns the scroll.
        @param factor  What to multiply the current scale by.
        @param anchor  The point in the grid to keep still, in this component's coordinates.
    */
    std::function<void (juce::Point<float> factor, juce::Point<float> anchor)> onZoomRequested;

    /** @brief Sets the notes that snapping lands on.
        @param rootNote    The key's tonic as a pitch class, zero being C.
        @param degreeMask  A bit per semitone above the tonic; all twelve means chromatic.
    */
    void setScale (int rootNote, int degreeMask);

    /** @brief Shows where the host's transport is, or hides the line when given a time before zero. */
    void setPlayheadSeconds (double seconds);

    /** @brief Rounds a note onto the nearest degree of the current scale. */
    [[nodiscard]] float snapToScale (float midiNote) const;

    /** @brief Moves every note being worked on onto the nearest semitone. */
    void snapSelection();

    /** @brief Moves every note being worked on by an interval.
        @param semitones  How far to move, which the arrow keys pass as one row.
    */
    void nudgeSelection (float semitones);

    /** @brief Sets how much of the sung contour the notes being worked on keep.
        @param depth  One keeps all of it, zero is dead flat.
    */
    void setSelectionDepth (float depth);

    /** @brief Puts every note being worked on back the way it was sung. */
    void resetSelection();

    /** @brief How many notes are selected, where none means the tools act on all of them. */
    [[nodiscard]] int getNumSelected() const;

    /** @brief The depth the tools would show for what is selected. */
    [[nodiscard]] float getSelectionDepth() const;

    [[nodiscard]] bool canUndo() const noexcept { return ! past.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return ! future.empty(); }

    void undo();
    void redo();

    /** @brief Sets the zoom on both axes and resizes to match.
        @param pixelsPerSecond  Horizontal scale.
        @param rowHeight        Height of one semitone row.
    */
    void setZoom (float pixelsPerSecond, float rowHeight);

    [[nodiscard]] float getPixelsPerSecond() const noexcept { return pixelsPerSecond; }
    [[nodiscard]] float getRowHeight() const noexcept { return rowHeight; }

    /** @brief Returns the size the grid wants at the current zoom, given the space available. */
    [[nodiscard]] juce::Point<int> getPreferredSize (int minimumWidth) const;

    /** @brief Returns the vertical centre of a note's row, in this component's coordinates. */
    [[nodiscard]] float getRowCentre (float midiNote) const;

    /** @brief Maps between this component's coordinates and the take's time and pitch. */
    [[nodiscard]] float getXForSeconds (double seconds) const;
    [[nodiscard]] double getSecondsForX (float x) const;
    [[nodiscard]] float getMidiNoteForY (float y) const;

    /** @brief Returns the length of what is shown, in seconds. */
    [[nodiscard]] double getSeconds() const;

    /** @brief Returns the note the sung range centres on, for scrolling the view to it. */
    [[nodiscard]] int getCentreNote() const;

    void paint (juce::Graphics& graphics) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    /** @brief What the mouse is doing to a note between pressing and releasing. */
    enum class Drag
    {
        none,
        pitch,
        boundary,
        handle,
        band
    };

    [[nodiscard]] std::optional<float> getYForFrequency (float frequencyHz) const;
    [[nodiscard]] float getXForFrame (int frameIndex) const;

    /** @brief The transposition the panel has to add to draw a note where it will be heard. */
    [[nodiscard]] float getDisplayShift() const;

    [[nodiscard]] juce::Rectangle<float> getNoteBounds (const EditedNote& note) const;

    [[nodiscard]] int getNoteAt (juce::Point<float> position) const;

    [[nodiscard]] bool isNearBoundary (const EditedNote& note, float x) const;

    /** @brief The note the handles belong to, or -1 when they are not shown. */
    [[nodiscard]] int getHandledNote() const;

    [[nodiscard]] juce::Rectangle<float> getHandleBounds (const EditedNote& note, Handle which) const;

    [[nodiscard]] Handle getHandleAt (juce::Point<float> position) const;

    [[nodiscard]] std::vector<int> getNotesToWorkOn() const;

    /** @brief Re-reads what a note was sung at, after its boundaries have moved. */
    void refreshSungNote (int noteIndex);

    void beginGesture();
    void commitGesture();

    void selectOnly (int noteIndex);

    void paintRows (juce::Graphics& graphics) const;
    void paintTimeGrid (juce::Graphics& graphics) const;
    void paintVoice (juce::Graphics& graphics) const;
    void paintNotes (juce::Graphics& graphics) const;
    void paintHandles (juce::Graphics& graphics) const;
    void paintPlayhead (juce::Graphics& graphics) const;
    void paintCurve (juce::Graphics& graphics,
                     const std::vector<float>& track,
                     juce::Colour colour,
                     float thickness) const;

    void rebuild();

    ConversionPointer conversion;
    std::vector<float> amplitude;

    PitchEdit edit;
    std::vector<char> selection;

    std::vector<PitchEdit> past;
    std::vector<PitchEdit> future;

    Tool tool { Tool::select };

    Drag drag { Drag::none };
    int draggedNote { -1 };
    float dragStartMidiNote { 0.0f };
    std::vector<int> dragNotes;
    std::vector<float> dragStartOffsets;

    Handle grabbedHandle { Handle::none };
    float grabbedHandleValue { 0.0f };

    juce::Point<float> bandOrigin;
    juce::Rectangle<float> band;
    std::vector<char> bandStartSelection;

    int scaleRoot { 0 };
    int scaleMask { 0xfff };

    double playheadSeconds { -1.0 };

    float pixelsPerSecond { 78.0f };
    float rowHeight { 9.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchTrack)
};

} // namespace rvcara
