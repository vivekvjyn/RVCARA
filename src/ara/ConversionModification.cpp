#include "ara/ConversionModification.h"

#include <utility>

namespace rvcara
{
bool ConversionModification::setSettings (const ConversionSettings& newSettings)
{
    if (settings == newSettings)
        return false;

    settings = newSettings;
    return true;
}

bool ConversionModification::setVoiceName (const juce::String& name)
{
    if (voiceName == name)
        return false;

    voiceName = name;
    return true;
}

PitchEdit ConversionModification::getPitchEdit() const
{
    const juce::ScopedLock lock { editLock };
    return pitchEdit;
}

bool ConversionModification::setPitchEdit (PitchEdit newEdit)
{
    const juce::ScopedLock lock { editLock };

    if (pitchEdit == newEdit)
        return false;

    pitchEdit = std::move (newEdit);
    return true;
}

bool ConversionModification::hasNotes() const
{
    const juce::ScopedLock lock { editLock };
    return ! pitchEdit.notes.empty();
}

void ConversionModification::setConversion (ConversionPointer newConversion)
{
    conversion = std::move (newConversion);
}

void ConversionModification::clearConversion()
{
    conversion.reset();
}

bool ConversionModification::isConversionCurrent (double sampleRate) const
{
    return conversion != nullptr
        && ! conversion->isPartial
        && conversion->voiceName == voiceName
        && conversion->settings == settings
        && conversion->pitchEdit == getPitchEdit()
        && juce::approximatelyEqual (conversion->sampleRate, sampleRate);
}

juce::String ConversionModification::getError() const
{
    const juce::ScopedLock lock { errorLock };
    return errorMessage;
}

void ConversionModification::setError (const juce::String& message)
{
    const juce::ScopedLock lock { errorLock };
    errorMessage = message;
}

juce::String ConversionModification::getNoteError() const
{
    const juce::ScopedLock lock { errorLock };
    return noteErrorMessage;
}

void ConversionModification::setNoteError (const juce::String& message)
{
    const juce::ScopedLock lock { errorLock };
    noteErrorMessage = message;
}

void ConversionModification::writeNotes (juce::OutputStream& stream, const PitchEdit& edit)
{
    stream.writeInt (static_cast<int> (edit.notes.size()));

    for (const auto& note : edit.notes)
    {
        stream.writeDouble (note.startSeconds);
        stream.writeDouble (note.endSeconds);
        stream.writeFloat (note.sungMidiNote);
        stream.writeFloat (note.offsetSemitones);
        stream.writeFloat (note.depth);
        stream.writeFloat (note.tiltLeft);
        stream.writeFloat (note.tiltRight);
        stream.writeBool (note.isRest);
    }
}

PitchEdit ConversionModification::readNotes (juce::InputStream& stream, int version)
{
    PitchEdit edit;

    // A take is a few hundred notes; anything past this is a damaged archive, not a performance.
    static constexpr int maximumNotes = 1 << 20;

    const auto numNotes = stream.readInt();

    if (numNotes <= 0 || numNotes > maximumNotes)
        return edit;

    edit.notes.reserve (static_cast<std::size_t> (numNotes));

    for (int noteIndex = 0; noteIndex < numNotes && ! stream.isExhausted(); ++noteIndex)
    {
        EditedNote note;
        note.startSeconds = stream.readDouble();
        note.endSeconds = stream.readDouble();
        note.sungMidiNote = stream.readFloat();
        note.offsetSemitones = stream.readFloat();
        note.depth = stream.readFloat();

        if (version >= 3)
        {
            note.tiltLeft = stream.readFloat();
            note.tiltRight = stream.readFloat();
        }

        note.isRest = stream.readBool();

        edit.notes.push_back (note);
    }

    return edit;
}

void ConversionModification::writeToArchive (juce::OutputStream& stream) const
{
    stream.writeInt (archiveVersion);
    stream.writeString (voiceName);
    stream.writeFloat (settings.pitchShiftSemitones);
    stream.writeFloat (settings.retrievalRatio);
    stream.writeFloat (settings.consonantProtection);
    stream.writeFloat (settings.envelopeFollowRatio);
    stream.writeInt (settings.latentNoiseSeed);
    stream.writeBool (settings.isBypassed);
    writeNotes (stream, getPitchEdit());
}

bool ConversionModification::readFromArchive (juce::InputStream& stream)
{
    const auto version = stream.readInt();

    if (version < 1 || version > archiveVersion)
        return false;

    const auto storedVoiceName = stream.readString();

    ConversionSettings storedSettings;
    storedSettings.pitchShiftSemitones = stream.readFloat();
    storedSettings.retrievalRatio = stream.readFloat();
    storedSettings.consonantProtection = stream.readFloat();
    storedSettings.envelopeFollowRatio = stream.readFloat();
    storedSettings.latentNoiseSeed = stream.readInt();
    storedSettings.isBypassed = stream.readBool();

    voiceName = storedVoiceName;
    settings = storedSettings;

    if (version >= 2)
    {
        auto storedEdit = readNotes (stream, version);

        if (! storedEdit.notes.empty())
            setNoteState (NoteState::found);

        const juce::ScopedLock lock { editLock };
        pitchEdit = std::move (storedEdit);
    }

    return true;
}

void ConversionModification::readAndDiscard (juce::InputStream& stream)
{
    const auto version = stream.readInt();
    stream.readString();
    stream.readFloat();
    stream.readFloat();
    stream.readFloat();
    stream.readFloat();
    stream.readInt();
    stream.readBool();

    if (version >= 2)
        readNotes (stream, version);
}
}
