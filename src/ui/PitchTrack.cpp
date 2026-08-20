#include "ui/PitchTrack.h"

#include "dsp/PitchConversions.h"
#include "ui/PanelLookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rvcara
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using Metrics = PanelLookAndFeel::Metrics;

    constexpr float waveformRows = 4.0f;

    /** @brief How close to a note's edge a press has to be to drag the boundary instead. */
    constexpr float edgeGrabPixels = 4.0f;

    /** @brief The shortest a note may be dragged down to, so a boundary cannot swallow it. */
    constexpr double minimumNoteSeconds = 0.02;

    /** @brief A note row is thin at low zoom, so give the mouse something to aim at. */
    constexpr float minimumGrabHeight = 9.0f;

    constexpr std::size_t maximumUndoSteps = 64;

    /** @brief The three controls PitchNet puts over a selected note, at the size it uses. */
    constexpr float handleWidth = 26.0f;
    constexpr float handleHeight = 11.0f;
    constexpr float handleGap = 4.0f;
    constexpr float handleLift = 4.0f;

    /** @brief How far a drag has to travel to move the shape control across its whole range. */
    constexpr float shapeSemitonesPerUnit = 4.0f;

    constexpr float maximumTiltSemitones = 24.0f;
    constexpr float maximumDepth = 2.0f;

    constexpr float zoomPerWheelNotch = 0.3f;

    /** @brief A band has to be dragged this far before it selects rather than deselects. */
    constexpr float minimumBandPixels = 3.0f;

    bool isBlackKey (int midiNote)
    {
        static const bool black[] = { false, true, false, true, false, false, true, false, true, false, true, false };
        return black[static_cast<std::size_t> (((midiNote % 12) + 12) % 12)];
    }
} // namespace

PitchTrack::PitchTrack()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
}

void PitchTrack::setConversion (ConversionPointer newConversion)
{
    if (conversion == newConversion)
        return;

    conversion = std::move (newConversion);
    rebuild();
    repaint();
}

void PitchTrack::setPitchEdit (PitchEdit newEdit)
{
    if (drag != Drag::none || edit == newEdit)
        return;

    edit = std::move (newEdit);
    selection.assign (edit.notes.size(), 0);
    past.clear();
    future.clear();

    if (onSelectionChanged != nullptr)
        onSelectionChanged();

    repaint();
}

void PitchTrack::setTool (Tool newTool)
{
    if (tool == newTool)
        return;

    tool = newTool;
    repaint();
}

void PitchTrack::setScale (int rootNote, int degreeMask)
{
    if (scaleRoot == rootNote && scaleMask == degreeMask)
        return;

    scaleRoot = rootNote;
    scaleMask = degreeMask;
    repaint();
}

void PitchTrack::setPlayheadSeconds (double seconds)
{
    if (juce::approximatelyEqual (playheadSeconds, seconds))
        return;

    playheadSeconds = seconds;
    repaint();
}

float PitchTrack::snapToScale (float midiNote) const
{
    const auto nearest = std::round (midiNote);

    if (scaleMask == 0 || (scaleMask & 0xfff) == 0xfff)
        return nearest;

    auto best = nearest;
    auto bestDistance = std::numeric_limits<float>::max();

    for (auto candidate = nearest - 2.0f; candidate <= nearest + 2.0f; candidate += 1.0f)
    {
        const auto degree = ((static_cast<int> (candidate) - scaleRoot) % 12 + 12) % 12;

        if ((scaleMask & (1 << degree)) == 0)
            continue;

        if (const auto distance = std::abs (candidate - midiNote); distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate;
        }
    }

    return best;
}

void PitchTrack::setZoom (float newPixelsPerSecond, float newRowHeight)
{
    pixelsPerSecond = juce::jlimit (minimumPixelsPerSecond, maximumPixelsPerSecond, newPixelsPerSecond);
    rowHeight = juce::jlimit (minimumRowHeight, maximumRowHeight, newRowHeight);
    repaint();
}

double PitchTrack::getSeconds() const
{
    auto seconds = 0.0;

    if (conversion != nullptr && conversion->pitchFrameRate > 0.0)
        seconds = static_cast<double> (conversion->fundamentalFrequencyHz.size()) / conversion->pitchFrameRate;

    for (const auto& note : edit.notes)
        seconds = std::max (seconds, note.endSeconds);

    return seconds;
}

float PitchTrack::getDisplayShift() const
{
    return conversion != nullptr ? conversion->settings.pitchShiftSemitones : 0.0f;
}

int PitchTrack::getCentreNote() const
{
    auto lowest = waveformNote;
    auto highest = waveformNote;

    const auto include = [&lowest, &highest] (float midiNote)
    {
        const auto row = juce::jlimit (lowestNote, highestNote, juce::roundToInt (midiNote));
        lowest = std::min (lowest, row);
        highest = std::max (highest, row);
    };

    if (conversion != nullptr)
        for (const auto frequencyHz : conversion->fundamentalFrequencyHz)
            if (frequencyHz > 0.0f)
                include (static_cast<float> (hzToMidiNote (static_cast<double> (frequencyHz))));

    const auto shift = getDisplayShift();

    for (const auto& note : edit.notes)
        if (! note.isRest)
            include (note.getEditedMidiNote() + shift);

    return (lowest + highest) / 2;
}

juce::Point<int> PitchTrack::getPreferredSize (int minimumWidth) const
{
    return { std::max (juce::roundToInt (getSeconds() * static_cast<double> (pixelsPerSecond)), minimumWidth),
             juce::roundToInt (rowHeight * static_cast<float> (numRows)) };
}

float PitchTrack::getRowCentre (float midiNote) const
{
    return static_cast<float> (getHeight()) - (midiNote - static_cast<float> (lowestNote) + 0.5f) * rowHeight;
}

float PitchTrack::getMidiNoteForY (float y) const
{
    return static_cast<float> (lowestNote) - 0.5f + (static_cast<float> (getHeight()) - y) / rowHeight;
}

std::optional<float> PitchTrack::getYForFrequency (float frequencyHz) const
{
    if (frequencyHz <= 0.0f)
        return std::nullopt;

    const auto midiNote = static_cast<float> (hzToMidiNote (static_cast<double> (frequencyHz)));

    if (midiNote < static_cast<float> (lowestNote) - 0.5f || midiNote > static_cast<float> (highestNote) + 0.5f)
        return std::nullopt;

    return getRowCentre (midiNote);
}

float PitchTrack::getXForSeconds (double seconds) const
{
    return static_cast<float> (seconds * static_cast<double> (pixelsPerSecond));
}

double PitchTrack::getSecondsForX (float x) const
{
    return pixelsPerSecond > 0.0f ? static_cast<double> (x / pixelsPerSecond) : 0.0;
}

float PitchTrack::getXForFrame (int frameIndex) const
{
    if (conversion == nullptr || conversion->pitchFrameRate <= 0.0)
        return 0.0f;

    return getXForSeconds (static_cast<double> (frameIndex) / conversion->pitchFrameRate);
}

juce::Rectangle<float> PitchTrack::getNoteBounds (const EditedNote& note) const
{
    const auto first = getXForSeconds (note.startSeconds);
    const auto last = getXForSeconds (note.endSeconds);
    const auto centre = getRowCentre (note.getEditedMidiNote() + getDisplayShift());

    return { first, centre - rowHeight * 0.5f, std::max (last - first, 1.0f), rowHeight };
}

int PitchTrack::getNoteAt (juce::Point<float> position) const
{
    for (int index = static_cast<int> (edit.notes.size()) - 1; index >= 0; --index)
    {
        const auto& note = edit.notes[static_cast<std::size_t> (index)];

        if (note.isRest)
            continue;

        auto bounds = getNoteBounds (note);

        if (bounds.getHeight() < minimumGrabHeight)
            bounds = bounds.withSizeKeepingCentre (bounds.getWidth(), minimumGrabHeight);

        if (bounds.expanded (edgeGrabPixels, 0.0f).contains (position))
            return index;
    }

    return -1;
}

bool PitchTrack::isNearBoundary (const EditedNote& note, float x) const
{
    return std::abs (getXForSeconds (note.endSeconds) - x) <= edgeGrabPixels;
}

int PitchTrack::getHandledNote() const
{
    auto only = -1;

    for (std::size_t index = 0; index < selection.size() && index < edit.notes.size(); ++index)
    {
        if (selection[index] == 0 || edit.notes[index].isRest)
            continue;

        if (only >= 0)
            return -1;

        only = static_cast<int> (index);
    }

    return only;
}

juce::Rectangle<float> PitchTrack::getHandleBounds (const EditedNote& note, Handle which) const
{
    const auto bounds = getNoteBounds (note);
    const auto groupWidth = handleWidth * 3.0f + handleGap * 2.0f;
    const auto left = bounds.getCentreX() - groupWidth * 0.5f;

    const auto column = which == Handle::tiltLeft ? 0.0f
                      : which == Handle::shape    ? 1.0f
                                                  : 2.0f;

    return { left + (handleWidth + handleGap) * column,
             bounds.getY() - handleHeight - handleLift,
             handleWidth,
             handleHeight };
}

PitchTrack::Handle PitchTrack::getHandleAt (juce::Point<float> position) const
{
    const auto handled = getHandledNote();

    if (handled < 0)
        return Handle::none;

    const auto& note = edit.notes[static_cast<std::size_t> (handled)];

    for (const auto which : { Handle::tiltLeft, Handle::shape, Handle::tiltRight })
        if (getHandleBounds (note, which).expanded (2.0f).contains (position))
            return which;

    return Handle::none;
}

std::vector<int> PitchTrack::getNotesToWorkOn() const
{
    std::vector<int> indices;

    for (std::size_t index = 0; index < edit.notes.size(); ++index)
        if (! edit.notes[index].isRest && index < selection.size() && selection[index] != 0)
            indices.push_back (static_cast<int> (index));

    if (! indices.empty())
        return indices;

    for (std::size_t index = 0; index < edit.notes.size(); ++index)
        if (! edit.notes[index].isRest)
            indices.push_back (static_cast<int> (index));

    return indices;
}

int PitchTrack::getNumSelected() const
{
    return static_cast<int> (std::count (selection.begin(), selection.end(), static_cast<char> (1)));
}

float PitchTrack::getSelectionDepth() const
{
    const auto indices = getNotesToWorkOn();

    if (indices.empty())
        return 1.0f;

    auto sum = 0.0f;

    for (const auto index : indices)
        sum += edit.notes[static_cast<std::size_t> (index)].depth;

    return sum / static_cast<float> (indices.size());
}

void PitchTrack::refreshSungNote (int noteIndex)
{
    if (conversion == nullptr || conversion->sourceFundamentalFrequencyHz.empty())
        return;

    auto& note = edit.notes[static_cast<std::size_t> (noteIndex)];

    const auto sung = getSungMidiNote (conversion->sourceFundamentalFrequencyHz,
                                       conversion->pitchFrameRate,
                                       0,
                                       note.startSeconds,
                                       note.endSeconds);

    if (sung > 0.0f)
        note.sungMidiNote = sung;
}

void PitchTrack::beginGesture()
{
    past.push_back (edit);
    future.clear();

    if (past.size() > maximumUndoSteps)
        past.erase (past.begin());
}

void PitchTrack::commitGesture()
{
    if (! past.empty() && past.back() == edit)
    {
        past.pop_back();
        return;
    }

    if (onEditChanged != nullptr)
        onEditChanged (edit);

    repaint();
}

void PitchTrack::undo()
{
    if (past.empty())
        return;

    future.push_back (edit);
    edit = past.back();
    past.pop_back();
    selection.resize (edit.notes.size(), 0);

    if (onEditChanged != nullptr)
        onEditChanged (edit);

    repaint();
}

void PitchTrack::redo()
{
    if (future.empty())
        return;

    past.push_back (edit);
    edit = future.back();
    future.pop_back();
    selection.resize (edit.notes.size(), 0);

    if (onEditChanged != nullptr)
        onEditChanged (edit);

    repaint();
}

void PitchTrack::selectOnly (int noteIndex)
{
    selection.assign (edit.notes.size(), 0);

    if (noteIndex >= 0)
        selection[static_cast<std::size_t> (noteIndex)] = 1;

    if (onSelectionChanged != nullptr)
        onSelectionChanged();
}

void PitchTrack::snapSelection()
{
    const auto indices = getNotesToWorkOn();

    if (indices.empty())
        return;

    beginGesture();

    const auto shift = getDisplayShift();

    for (const auto index : indices)
    {
        auto& note = edit.notes[static_cast<std::size_t> (index)];
        const auto heard = note.getEditedMidiNote() + shift;
        note.offsetSemitones += snapToScale (heard) - heard;
    }

    commitGesture();
}

void PitchTrack::nudgeSelection (float semitones)
{
    const auto indices = getNotesToWorkOn();

    if (indices.empty())
        return;

    beginGesture();

    for (const auto index : indices)
        edit.notes[static_cast<std::size_t> (index)].offsetSemitones += semitones;

    commitGesture();
}

void PitchTrack::setSelectionDepth (float depth)
{
    const auto indices = getNotesToWorkOn();

    if (indices.empty())
        return;

    beginGesture();

    for (const auto index : indices)
        edit.notes[static_cast<std::size_t> (index)].depth = juce::jlimit (0.0f, 1.0f, depth);

    commitGesture();
}

void PitchTrack::resetSelection()
{
    const auto indices = getNotesToWorkOn();

    if (indices.empty())
        return;

    beginGesture();

    for (const auto index : indices)
    {
        auto& note = edit.notes[static_cast<std::size_t> (index)];
        note.offsetSemitones = 0.0f;
        note.depth = 1.0f;
        note.tiltLeft = 0.0f;
        note.tiltRight = 0.0f;
    }

    commitGesture();
}

void PitchTrack::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    const auto position = event.position;

    if (tool == Tool::select)
    {
        if (const auto which = getHandleAt (position); which != Handle::none)
        {
            const auto handled = getHandledNote();
            const auto& note = edit.notes[static_cast<std::size_t> (handled)];

            beginGesture();

            drag = Drag::handle;
            draggedNote = handled;
            grabbedHandle = which;
            grabbedHandleValue = which == Handle::tiltLeft  ? note.tiltLeft
                               : which == Handle::tiltRight ? note.tiltRight
                                                            : note.depth;
            dragStartMidiNote = getMidiNoteForY (position.y);
            return;
        }
    }

    const auto index = getNoteAt (position);

    if (index < 0)
    {
        if (tool != Tool::select)
        {
            selectOnly (-1);
            repaint();
            return;
        }

        if (! event.mods.isShiftDown() && ! event.mods.isCommandDown())
            selection.assign (edit.notes.size(), 0);

        bandStartSelection = selection;
        bandOrigin = position;
        band = { position.x, position.y, 0.0f, 0.0f };
        drag = Drag::band;

        if (onSelectionChanged != nullptr)
            onSelectionChanged();

        repaint();
        return;
    }

    auto& note = edit.notes[static_cast<std::size_t> (index)];

    if (tool == Tool::split)
    {
        const auto splitSeconds = std::clamp (getSecondsForX (position.x),
                                              note.startSeconds + minimumNoteSeconds,
                                              note.endSeconds - minimumNoteSeconds);

        if (splitSeconds <= note.startSeconds || splitSeconds >= note.endSeconds)
            return;

        beginGesture();

        auto second = note;
        second.startSeconds = splitSeconds;
        note.endSeconds = splitSeconds;

        edit.notes.insert (edit.notes.begin() + index + 1, second);
        selection.assign (edit.notes.size(), 0);

        refreshSungNote (index);
        refreshSungNote (index + 1);

        commitGesture();
        return;
    }

    if (tool == Tool::glue)
    {
        if (index + 1 >= static_cast<int> (edit.notes.size()))
            return;

        beginGesture();

        const auto& next = edit.notes[static_cast<std::size_t> (index) + 1];
        note.endSeconds = next.endSeconds;
        note.isRest = note.isRest && next.isRest;

        edit.notes.erase (edit.notes.begin() + index + 1);
        selection.assign (edit.notes.size(), 0);

        refreshSungNote (index);

        commitGesture();
        return;
    }

    const auto hasNext = index + 1 < static_cast<int> (edit.notes.size());
    const auto nearOwnEnd = isNearBoundary (note, position.x);
    const auto nearOwnStart = index > 0
                           && isNearBoundary (edit.notes[static_cast<std::size_t> (index) - 1], position.x);

    if (nearOwnEnd && hasNext)
    {
        beginGesture();
        drag = Drag::boundary;
        draggedNote = index;
        return;
    }

    if (nearOwnStart)
    {
        beginGesture();
        drag = Drag::boundary;
        draggedNote = index - 1;
        return;
    }

    const auto isAlreadySelected = selection[static_cast<std::size_t> (index)] != 0;

    if (event.mods.isShiftDown() || event.mods.isCommandDown())
    {
        selection[static_cast<std::size_t> (index)] = isAlreadySelected ? 0 : 1;

        if (onSelectionChanged != nullptr)
            onSelectionChanged();

        repaint();
        return;
    }
    else if (! isAlreadySelected)
    {
        selectOnly (index);
    }

    beginGesture();

    drag = Drag::pitch;
    draggedNote = index;
    dragStartMidiNote = getMidiNoteForY (position.y);
    dragNotes = getNotesToWorkOn();

    dragStartOffsets.clear();
    dragStartOffsets.reserve (dragNotes.size());

    for (const auto moving : dragNotes)
        dragStartOffsets.push_back (edit.notes[static_cast<std::size_t> (moving)].offsetSemitones);

    repaint();
}

void PitchTrack::mouseDrag (const juce::MouseEvent& event)
{
    if (drag == Drag::band)
    {
        band = juce::Rectangle<float>::leftTopRightBottom (
            std::min (bandOrigin.x, event.position.x), std::min (bandOrigin.y, event.position.y),
            std::max (bandOrigin.x, event.position.x), std::max (bandOrigin.y, event.position.y));

        selection = bandStartSelection;
        selection.resize (edit.notes.size(), 0);

        if (band.getWidth() >= minimumBandPixels || band.getHeight() >= minimumBandPixels)
            for (std::size_t index = 0; index < edit.notes.size(); ++index)
                if (! edit.notes[index].isRest
                    && getNoteBounds (edit.notes[index]).expanded (0.0f, minimumGrabHeight * 0.5f)
                           .intersects (band))
                    selection[index] = 1;

        if (onSelectionChanged != nullptr)
            onSelectionChanged();

        repaint();
        return;
    }

    if (drag == Drag::none || draggedNote < 0)
        return;

    if (drag == Drag::handle)
    {
        const auto delta = getMidiNoteForY (event.position.y) - dragStartMidiNote;
        auto& note = edit.notes[static_cast<std::size_t> (draggedNote)];

        if (grabbedHandle == Handle::shape)
            note.depth = juce::jlimit (0.0f, maximumDepth,
                                       grabbedHandleValue + delta / shapeSemitonesPerUnit);
        else if (grabbedHandle == Handle::tiltLeft)
            note.tiltLeft = juce::jlimit (-maximumTiltSemitones, maximumTiltSemitones,
                                          grabbedHandleValue + delta);
        else
            note.tiltRight = juce::jlimit (-maximumTiltSemitones, maximumTiltSemitones,
                                           grabbedHandleValue + delta);

        repaint();
        return;
    }

    if (drag == Drag::boundary)
    {
        auto& note = edit.notes[static_cast<std::size_t> (draggedNote)];
        auto& next = edit.notes[static_cast<std::size_t> (draggedNote) + 1];

        const auto moved = std::clamp (getSecondsForX (event.position.x),
                                       note.startSeconds + minimumNoteSeconds,
                                       next.endSeconds - minimumNoteSeconds);

        note.endSeconds = moved;
        next.startSeconds = moved;

        refreshSungNote (draggedNote);
        refreshSungNote (draggedNote + 1);

        repaint();
        return;
    }

    auto delta = getMidiNoteForY (event.position.y) - dragStartMidiNote;

    if (! event.mods.isShiftDown())
    {
        const auto& primary = edit.notes[static_cast<std::size_t> (draggedNote)];
        const auto moving = std::find (dragNotes.begin(), dragNotes.end(), draggedNote);

        if (moving != dragNotes.end())
        {
            const auto startOffset = dragStartOffsets[static_cast<std::size_t> (
                std::distance (dragNotes.begin(), moving))];
            const auto heard = primary.sungMidiNote + startOffset + delta + getDisplayShift();
            delta += snapToScale (heard) - heard;
        }
    }

    for (std::size_t index = 0; index < dragNotes.size(); ++index)
        edit.notes[static_cast<std::size_t> (dragNotes[index])].offsetSemitones =
            dragStartOffsets[index] + delta;

    repaint();
}

void PitchTrack::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::none)
        return;

    const auto wasBanding = drag == Drag::band;

    drag = Drag::none;
    draggedNote = -1;
    grabbedHandle = Handle::none;
    dragNotes.clear();
    dragStartOffsets.clear();
    band = {};

    if (wasBanding)
    {
        repaint();
        return;
    }

    commitGesture();
}

void PitchTrack::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (tool != Tool::select)
        return;

    const auto index = getNoteAt (event.position);

    if (index < 0)
        return;

    selectOnly (index);
    snapSelection();
}

void PitchTrack::mouseMove (const juce::MouseEvent& event)
{
    if (tool == Tool::split)
    {
        setMouseCursor (juce::MouseCursor::IBeamCursor);
        return;
    }

    if (tool == Tool::glue)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        return;
    }

    if (getHandleAt (event.position) != Handle::none)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    const auto index = getNoteAt (event.position);

    if (index < 0)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    const auto& note = edit.notes[static_cast<std::size_t> (index)];
    const auto nearBoundary = isNearBoundary (note, event.position.x)
                           || (index > 0
                               && isNearBoundary (edit.notes[static_cast<std::size_t> (index) - 1],
                                                  event.position.x));

    setMouseCursor (nearBoundary ? juce::MouseCursor::LeftRightResizeCursor
                                 : juce::MouseCursor::UpDownResizeCursor);
}

void PitchTrack::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (! event.mods.isCommandDown() && ! event.mods.isCtrlDown())
    {
        Component::mouseWheelMove (event, wheel);
        return;
    }

    if (onZoomRequested == nullptr)
        return;

    const auto factor = 1.0f + wheel.deltaY * zoomPerWheelNotch;

    onZoomRequested (event.mods.isShiftDown() ? juce::Point<float> { 1.0f, factor }
                                              : juce::Point<float> { factor, 1.0f },
                     event.position);
}

bool PitchTrack::keyPressed (const juce::KeyPress& key)
{
    const auto step = key.getModifiers().isShiftDown() ? 0.1f : 1.0f;

    if (key.isKeyCode (juce::KeyPress::upKey))
    {
        nudgeSelection (step);
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::downKey))
    {
        nudgeSelection (-step);
        return true;
    }

    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        undo();
        return true;
    }

    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier
                                        | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0))
    {
        redo();
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey))
    {
        resetSelection();
        return true;
    }

    return false;
}

void PitchTrack::rebuild()
{
    amplitude.clear();

    if (conversion == nullptr || conversion->samples.empty() || conversion->pitchFrameRate <= 0.0)
        return;

    const auto numFrames = static_cast<int> (conversion->fundamentalFrequencyHz.size());
    const auto samplesPerFrame = conversion->sampleRate > 0.0
                               ? conversion->sampleRate / conversion->pitchFrameRate
                               : static_cast<double> (conversion->samples.size()) / std::max (numFrames, 1);

    amplitude.resize (static_cast<std::size_t> (numFrames), 0.0f);

    const auto numSamples = static_cast<int> (conversion->samples.size());
    auto loudest = 0.0f;

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto first = static_cast<int> (static_cast<double> (frameIndex) * samplesPerFrame);
        const auto last = std::min (static_cast<int> (static_cast<double> (frameIndex + 1) * samplesPerFrame),
                                    numSamples);

        auto peak = 0.0f;

        for (int sampleIndex = std::max (first, 0); sampleIndex < last; ++sampleIndex)
            peak = std::max (peak, std::abs (conversion->samples[static_cast<std::size_t> (sampleIndex)]));

        amplitude[static_cast<std::size_t> (frameIndex)] = peak;
        loudest = std::max (loudest, peak);
    }

    if (loudest <= 0.0f)
        return;

    for (auto& peak : amplitude)
        peak /= loudest;
}

void PitchTrack::paintRows (juce::Graphics& graphics) const
{
    const auto width = static_cast<float> (getWidth());

    for (int midiNote = lowestNote; midiNote <= highestNote; ++midiNote)
    {
        const auto top = getRowCentre (static_cast<float> (midiNote)) - rowHeight * 0.5f;

        if (isBlackKey (midiNote))
        {
            graphics.setColour (Palette::blackKeyRow);
            graphics.fillRect (0.0f, top, width, rowHeight);
        }

        graphics.setColour (midiNote % 12 == 0 ? Palette::edge : Palette::rule);
        graphics.fillRect (0.0f, top + rowHeight - Metrics::hairline, width, Metrics::hairline);
    }
}

void PitchTrack::paintTimeGrid (juce::Graphics& graphics) const
{
    const auto totalSeconds = getSeconds();
    const auto height = static_cast<float> (getHeight());
    const auto secondsPerLine = pixelsPerSecond < 40.0f ? 5.0 : 1.0;

    for (auto second = 0.0; second <= totalSeconds; second += secondsPerLine)
    {
        const auto x = getXForSeconds (second);

        graphics.setColour (std::fmod (second, 5.0) < 0.5 ? Palette::edge : Palette::rule);
        graphics.fillRect (x, 0.0f, Metrics::hairline, height);
    }
}

void PitchTrack::paintVoice (juce::Graphics& graphics) const
{
    if (amplitude.empty())
        return;

    const auto centre = getRowCentre (static_cast<float> (waveformNote));
    const auto halfHeight = rowHeight * waveformRows * 0.5f;
    const auto numFrames = static_cast<int> (amplitude.size());

    juce::Path ribbon;
    ribbon.startNewSubPath (getXForFrame (0), centre);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
        ribbon.lineTo (getXForFrame (frameIndex),
                       centre - amplitude[static_cast<std::size_t> (frameIndex)] * halfHeight);

    for (int frameIndex = numFrames - 1; frameIndex >= 0; --frameIndex)
        ribbon.lineTo (getXForFrame (frameIndex),
                       centre + amplitude[static_cast<std::size_t> (frameIndex)] * halfHeight);

    ribbon.closeSubPath();

    graphics.setColour (Palette::silhouette);
    graphics.fillPath (ribbon);
}

void PitchTrack::paintNotes (juce::Graphics& graphics) const
{
    const auto width = static_cast<float> (getWidth());

    for (std::size_t index = 0; index < edit.notes.size(); ++index)
    {
        const auto& note = edit.notes[index];

        if (note.isRest)
            continue;

        const auto bounds = getNoteBounds (note).reduced (0.0f, 0.5f);

        if (bounds.getRight() < 0.0f || bounds.getX() > width)
            continue;

        const auto isSelected = index < selection.size() && selection[index] != 0;
        const auto corner = std::min (2.5f, rowHeight * 0.4f);

        graphics.setColour (isSelected ? Palette::accent.withAlpha (0.55f) : Palette::noteBlock);
        graphics.fillRoundedRectangle (bounds, corner);

        graphics.setColour (isSelected ? Palette::accent : Palette::accentDim);
        graphics.drawRoundedRectangle (bounds, corner, Metrics::hairline);

        if (note.depth < 1.0f)
        {
            graphics.setColour (Palette::accent.withAlpha (1.0f - note.depth * 0.5f));
            graphics.fillRect (bounds.getX(), bounds.getCentreY() - 0.5f, bounds.getWidth(), 1.0f);
        }
    }
}

void PitchTrack::paintHandles (juce::Graphics& graphics) const
{
    const auto handled = getHandledNote();

    if (handled < 0)
        return;

    const auto& note = edit.notes[static_cast<std::size_t> (handled)];

    const auto draw = [&] (Handle which, const juce::String& glyph, bool isSet)
    {
        const auto bounds = getHandleBounds (note, which);

        if (bounds.getBottom() < 0.0f || bounds.getY() > static_cast<float> (getHeight()))
            return;

        graphics.setColour (isSet ? Palette::accent : Palette::bar);
        graphics.fillRoundedRectangle (bounds, 2.0f);

        graphics.setColour (Palette::accent);
        graphics.drawRoundedRectangle (bounds, 2.0f, Metrics::hairline);

        graphics.setColour (isSet ? Palette::ground : Palette::text);
        graphics.setFont (juce::Font { juce::FontOptions { handleHeight - 1.0f } });
        graphics.drawText (glyph, bounds, juce::Justification::centred, false);
    };

    draw (Handle::tiltLeft, "/", note.tiltLeft != 0.0f);
    draw (Handle::shape, "~", note.depth != 1.0f);
    draw (Handle::tiltRight, "\\", note.tiltRight != 0.0f);
}

void PitchTrack::paintPlayhead (juce::Graphics& graphics) const
{
    if (playheadSeconds < 0.0)
        return;

    graphics.setColour (Palette::accent);
    graphics.fillRect (getXForSeconds (playheadSeconds), 0.0f, 1.0f, static_cast<float> (getHeight()));
}

void PitchTrack::paintCurve (juce::Graphics& graphics,
                             const std::vector<float>& track,
                             juce::Colour colour,
                             float thickness) const
{
    if (track.empty())
        return;

    juce::Path curve;
    auto isDrawing = false;

    for (int frameIndex = 0; frameIndex < static_cast<int> (track.size()); ++frameIndex)
    {
        const auto y = getYForFrequency (track[static_cast<std::size_t> (frameIndex)]);

        if (! y.has_value())
        {
            isDrawing = false;
            continue;
        }

        const auto x = getXForFrame (frameIndex);

        if (isDrawing)
        {
            curve.lineTo (x, *y);
        }
        else
        {
            curve.startNewSubPath (x, *y);
            isDrawing = true;
        }
    }

    graphics.setColour (colour);
    graphics.strokePath (curve, juce::PathStrokeType { thickness, juce::PathStrokeType::curved });
}

void PitchTrack::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::well);

    paintRows (graphics);
    paintTimeGrid (graphics);
    paintVoice (graphics);
    paintNotes (graphics);

    if (conversion != nullptr)
    {
        paintCurve (graphics, conversion->sourceFundamentalFrequencyHz, Palette::sungCurve, 1.0f);
        paintCurve (graphics, conversion->fundamentalFrequencyHz, Palette::accent, 1.8f);
    }

    paintHandles (graphics);
    paintPlayhead (graphics);

    if (! band.isEmpty())
    {
        graphics.setColour (Palette::accent.withAlpha (0.15f));
        graphics.fillRect (band);
        graphics.setColour (Palette::accent);
        graphics.drawRect (band, Metrics::hairline);
    }
}

} // namespace rvcara
