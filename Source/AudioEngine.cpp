#include "AudioEngine.h"

AudioEngine::AudioEngine()
{
    // Register built-in formats (WAV, AIFF) so AudioFormatManager can decode them.
    formatManager.registerBasicFormats();

    // Initialise the device manager requesting up to 32 input and 32 output
    // channels so that all channels on a multi-channel interface are available
    // for routing. JUCE will open the system default device and clamp the
    // channel count to what the hardware actually supports.
    auto result = deviceManager.initialise (32, 32, nullptr, true);

    if (result.isNotEmpty())
        DBG ("AudioDeviceManager initialise error: " + result);

    // On macOS, initialise() may pick separate devices for input and output.
    // For measurement we need both I/O on the same hardware device.
    auto setup = deviceManager.getAudioDeviceSetup();

    if (setup.outputDeviceName.isNotEmpty() &&
        setup.inputDeviceName != setup.outputDeviceName)
    {
        setup.inputDeviceName         = setup.outputDeviceName;
        setup.useDefaultInputChannels = true;
        auto err = deviceManager.setAudioDeviceSetup (setup, true);

        if (err.isNotEmpty())
            DBG ("AudioDeviceManager input alignment error: " + err);
    }

    // Register this object as the audio callback. The callback is always
    // registered but does nothing until measuring == true.
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

//==============================================================================
juce::String AudioEngine::loadReferenceFile (const juce::File& file)
{
    auto* reader = formatManager.createReaderFor (file);
    if (reader == nullptr)
        return "Could not open file: " + file.getFileName();

    referenceFileStem    = file.getFileNameWithoutExtension();
    referenceNumChannels = (int) reader->numChannels;

    // AudioFormatReaderSource owns the reader and streams it on demand.
    referenceReaderSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);

    // ResamplingAudioSource converts from the file's native rate to the device
    // rate. The ratio is set correctly in audioDeviceAboutToStart(); here we
    // pass 1.0 as a placeholder.
    resamplingSource = std::make_unique<juce::ResamplingAudioSource> (
        referenceReaderSource.get(), false, referenceNumChannels);

    // If the device is already running, update the resampling ratio now.
    if (sampleRate > 0.0f && reader->sampleRate > 0)
        resamplingSource->setResamplingRatio (reader->sampleRate / (double) sampleRate);

    return {};   // success
}

//==============================================================================
juce::String AudioEngine::loadReferenceFileFromMemory (const void* data,
                                                        size_t dataSize,
                                                        const juce::String& stem)
{
    auto stream = std::make_unique<juce::MemoryInputStream> (data, dataSize, false);
    auto* reader = formatManager.createReaderFor (std::move (stream));

    if (reader == nullptr)
        return "Could not decode embedded reference file: " + stem;

    referenceFileStem    = stem;
    referenceNumChannels = (int) reader->numChannels;

    referenceReaderSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    resamplingSource = std::make_unique<juce::ResamplingAudioSource> (
        referenceReaderSource.get(), false, referenceNumChannels);

    if (sampleRate > 0.0f && reader->sampleRate > 0)
        resamplingSource->setResamplingRatio (reader->sampleRate / (double) sampleRate);

    return {};
}

//==============================================================================
void AudioEngine::startMeasurement()
{
    if (resamplingSource == nullptr)
        return;

    // Rewind the source to the beginning of the file.
    resamplingSource->prepareToPlay (512, sampleRate);
    referenceReaderSource->setNextReadPosition (0);

    // Pre-allocate capture buffers.
    // refBuffer matches the reference file's channel count (mono or stereo).
    // recBuffer is always stereo (captures both channels of the Return pair).
    const int maxSamples = maxRecordSeconds * (int) sampleRate;
    refBuffer.setSize (referenceNumChannels, maxSamples, false, true, false);
    recBuffer.setSize (2,                   maxSamples, false, true, false);
    capturePosition = 0;

    finished.store (false);
    measuring.store (true);
}

void AudioEngine::stopMeasurement()
{
    measuring.store (false);
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = static_cast<float> (device->getCurrentSampleRate());

    // Update the resampling ratio if a file is already loaded.
    if (resamplingSource != nullptr && referenceReaderSource != nullptr)
    {
        const double fileRate = referenceReaderSource->getAudioFormatReader()->sampleRate;
        resamplingSource->setResamplingRatio (fileRate / (double) sampleRate);
        resamplingSource->prepareToPlay (device->getCurrentBufferSizeSamples(), sampleRate);
    }
}

void AudioEngine::audioDeviceStopped()
{
    measuring.store (false);
}

//==============================================================================
void AudioEngine::audioDeviceIOCallbackWithContext (
    const float* const* inputChannelData,  int numInputChannels,
    float* const*       outputChannelData, int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    // Zero all output channels first so unused channels stay silent.
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    if (! measuring.load())
        return;

    if (resamplingSource == nullptr)
        return;

    const int sendCh = sendChannel  .load();
    const int monCh  = monitorChannel.load();   // -1 if not set
    const int retCh  = returnChannel .load();

    // Pull the next block of resampled samples from the reference source.
    juce::AudioBuffer<float> playBuf (referenceNumChannels, numSamples);
    juce::AudioSourceChannelInfo info (playBuf);
    resamplingSource->getNextAudioBlock (info);

    // --- Output: write to Send pair ---
    // Mono source: duplicate to both channels of the pair.
    // Stereo source: L → sendCh, R → sendCh+1.
    if (sendCh >= 0 && sendCh < numOutputChannels)
    {
        if (referenceNumChannels == 1)
        {
            if (outputChannelData[sendCh] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[sendCh],
                                                   playBuf.getReadPointer (0), numSamples);

            if (sendCh + 1 < numOutputChannels && outputChannelData[sendCh + 1] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[sendCh + 1],
                                                   playBuf.getReadPointer (0), numSamples);
        }
        else
        {
            if (outputChannelData[sendCh] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[sendCh],
                                                   playBuf.getReadPointer (0), numSamples);

            if (sendCh + 1 < numOutputChannels && outputChannelData[sendCh + 1] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[sendCh + 1],
                                                   playBuf.getReadPointer (1), numSamples);
        }
    }

    // --- Output: mirror to Monitor pair ---
    if (monCh >= 0 && monCh != sendCh && monCh < numOutputChannels)
    {
        if (outputChannelData[monCh] != nullptr)
            juce::FloatVectorOperations::copy (outputChannelData[monCh],
                                               playBuf.getReadPointer (0), numSamples);

        if (monCh + 1 < numOutputChannels && outputChannelData[monCh + 1] != nullptr)
        {
            const float* src = (referenceNumChannels > 1) ? playBuf.getReadPointer (1)
                                                           : playBuf.getReadPointer (0);
            juce::FloatVectorOperations::copy (outputChannelData[monCh + 1], src, numSamples);
        }
    }

    // --- Capture: write to refBuffer and recBuffer ---
    const int remaining = refBuffer.getNumSamples() - capturePosition;
    const int toWrite   = juce::jmin (numSamples, remaining);

    if (toWrite > 0)
    {
        // ref: copy from playBuf (what was sent out)
        for (int ch = 0; ch < referenceNumChannels; ++ch)
            refBuffer.copyFrom (ch, capturePosition, playBuf, ch, 0, toWrite);

        // rec: copy from Return input pair
        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = retCh + ch;
            if (srcCh < numInputChannels && inputChannelData[srcCh] != nullptr)
                recBuffer.copyFrom (ch, capturePosition, inputChannelData[srcCh], toWrite);
            else
                recBuffer.clear (ch, capturePosition, toWrite);
        }

        capturePosition += toWrite;
    }

    // --- Auto-stop when the source reaches EOF ---
    if (referenceReaderSource->getNextReadPosition() >=
        referenceReaderSource->getTotalLength())
    {
        measuring.store (false);
        finished.store  (true);
    }
}

//==============================================================================
void AudioEngine::trimRecBuffer (int lagSamples)
{
    if (lagSamples <= 0 || lagSamples >= capturePosition)
        return;

    const int newLength = capturePosition - lagSamples;

    for (int ch = 0; ch < recBuffer.getNumChannels(); ++ch)
    {
        float* data = recBuffer.getWritePointer (ch);
        // memmove handles the overlapping src/dest regions correctly.
        std::memmove (data, data + lagSamples, sizeof (float) * (size_t) newLength);
    }

    // Reduce capturePosition so writeSession() writes the same length for
    // both ref (trimmed from the end) and rec (trimmed from the start).
    capturePosition = newLength;
}

//==============================================================================
juce::String AudioEngine::writeSession (const juce::File& refFilePath,
                                         const juce::File& recFilePath)
{
    if (capturePosition == 0)
        return "No audio was captured.";

    juce::WavAudioFormat wav;

    // --- Write ref.wav ---
    {
        std::unique_ptr<juce::OutputStream> refStream = refFilePath.createOutputStream();

        if (refStream == nullptr)
            return "Could not create file: " + refFilePath.getFullPathName();

        auto writer = wav.createWriterFor (refStream,
                                           juce::AudioFormatWriterOptions{}
                                               .withSampleRate    (sampleRate)
                                               .withNumChannels   (refBuffer.getNumChannels())
                                               .withBitsPerSample (24));

        if (writer == nullptr)
            return "Could not create WAV writer for ref file.";

        writer->writeFromAudioSampleBuffer (refBuffer, 0, capturePosition);
    }

    // --- Write rec.wav ---
    {
        std::unique_ptr<juce::OutputStream> recStream = recFilePath.createOutputStream();

        if (recStream == nullptr)
            return "Could not create file: " + recFilePath.getFullPathName();

        auto writer = wav.createWriterFor (recStream,
                                           juce::AudioFormatWriterOptions{}
                                               .withSampleRate    (sampleRate)
                                               .withNumChannels   (recBuffer.getNumChannels())
                                               .withBitsPerSample (24));

        if (writer == nullptr)
            return "Could not create WAV writer for rec file.";

        writer->writeFromAudioSampleBuffer (recBuffer, 0, capturePosition);
    }

    return {};   // success
}
