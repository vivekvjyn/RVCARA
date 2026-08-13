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

void ConversionModification::setConversion (ConversionPointer newConversion)
{
    conversion = std::move (newConversion);
}

void ConversionModification::clearConversion()
{
    conversion.reset();
}

bool ConversionModification::isConversionCurrent() const noexcept
{
    return conversion != nullptr
        && conversion->voiceName == voiceName
        && conversion->settings == settings;
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
}

bool ConversionModification::readFromArchive (juce::InputStream& stream)
{
    const auto version = stream.readInt();

    if (version != archiveVersion)
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

    return true;
}

void ConversionModification::readAndDiscard (juce::InputStream& stream)
{
    stream.readInt();
    stream.readString();
    stream.readFloat();
    stream.readFloat();
    stream.readFloat();
    stream.readFloat();
    stream.readInt();
    stream.readBool();
}
}
