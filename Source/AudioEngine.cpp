#include "AudioEngine.h"
#include "LatencyAligner.h"

AudioEngine::AudioEngine()
{
    // Register built-in formats (WAV, AIFF) so AudioFormatManager can decode them.
    formatManager.registerBasicFormats();

    // Load previously saved device settings (device name, sample rate, buffer size).
    // If no settings file exists, nullptr causes JUCE to open the default device.
    std::unique_ptr<juce::XmlElement> savedState;
    const juce::File settingsFile = getSettingsFile();
    if (settingsFile.existsAsFile())
        savedState = juce::parseXML (settingsFile.loadFileAsString());

    // Initialise the device manager requesting up to 32 input and 32 output
    // channels so that all channels on a multi-channel interface are available
    // for routing. JUCE will open the system default device and clamp the
    // channel count to what the hardware actually supports.
    auto result = deviceManager.initialise (32, 32, savedState.get(), true);

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
float AudioEngine::getReturnPeakDb() const
{
    const float p = returnPeakLinear.load();
    return (p > 0.0f) ? 20.0f * std::log10 (p) : -100.0f;
}

float AudioEngine::getReturnRmsDb() const
{
    const float r = returnRmsLinear.load();
    return (r > 0.0f) ? 20.0f * std::log10 (r) : -100.0f;
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
    auto stream = std::make_unique<juce::MemoryInputStream> (data, dataSize, true);
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

    const int  sendCh = sendChannel  .load();
    const int  monCh  = monitorChannel.load();   // -1 if not set
    const int  retCh  = returnChannel .load();
    const bool mono   = monoMode      .load();

    // Pull the next block of resampled samples from the reference source.
    juce::AudioBuffer<float> playBuf (referenceNumChannels, numSamples);
    juce::AudioSourceChannelInfo info (playBuf);
    resamplingSource->getNextAudioBlock (info);

    // --- Output: write to Send channel(s) ---
    if (sendCh >= 0 && sendCh < numOutputChannels)
    {
        if (mono)
        {
            // Single channel only.
            if (outputChannelData[sendCh] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[sendCh],
                                                   playBuf.getReadPointer (0), numSamples);
        }
        else
        {
            // Stereo pair: duplicate mono source or split stereo source.
            if (outputChannelData[sendCh] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[sendCh],
                                                   playBuf.getReadPointer (0), numSamples);

            if (sendCh + 1 < numOutputChannels && outputChannelData[sendCh + 1] != nullptr)
            {
                const float* src = (referenceNumChannels > 1) ? playBuf.getReadPointer (1)
                                                               : playBuf.getReadPointer (0);
                juce::FloatVectorOperations::copy (outputChannelData[sendCh + 1],
                                                   src, numSamples);
            }
        }
    }

    // --- Output: mirror to Monitor channel(s) ---
    if (monCh >= 0 && monCh != sendCh && monCh < numOutputChannels)
    {
        if (outputChannelData[monCh] != nullptr)
            juce::FloatVectorOperations::copy (outputChannelData[monCh],
                                               playBuf.getReadPointer (0), numSamples);

        if (!mono && monCh + 1 < numOutputChannels && outputChannelData[monCh + 1] != nullptr)
        {
            const float* src = (referenceNumChannels > 1) ? playBuf.getReadPointer (1)
                                                           : playBuf.getReadPointer (0);
            juce::FloatVectorOperations::copy (outputChannelData[monCh + 1], src, numSamples);
        }
    }

    // --- Continuous input peak metering (always, not just while measuring) ---
    {
        float peak = 0.0f;
        const int meterCh = retCh;
        if (meterCh >= 0 && meterCh < numInputChannels && inputChannelData[meterCh] != nullptr)
            for (int n = 0; n < numSamples; ++n)
                peak = std::max (peak, std::abs (inputChannelData[meterCh][n]));
        returnPeakLinear.store (peak);

        // RMS: leaky integrator over squared samples (~300ms time constant)
        float sumSq = 0.0f;
        if (meterCh >= 0 && meterCh < numInputChannels && inputChannelData[meterCh] != nullptr)
            for (int n = 0; n < numSamples; ++n)
                sumSq += inputChannelData[meterCh][n] * inputChannelData[meterCh][n];
        const float blockMeanSq = (numSamples > 0) ? (sumSq / (float) numSamples) : 0.0f;
        // alpha ≈ 0.95 at 512/48kHz ≈ 10ms per block → ~200ms effective window
        rmsAccum = 0.95f * rmsAccum + 0.05f * blockMeanSq;
        returnRmsLinear.store (std::sqrt (rmsAccum));
    }

    // --- Capture: write to refBuffer and recBuffer ---
    const int remaining = refBuffer.getNumSamples() - capturePosition;
    const int toWrite   = juce::jmin (numSamples, remaining);

    if (toWrite > 0)
    {
        // ref: copy from playBuf (what was sent out), always channel 0.
        for (int ch = 0; ch < referenceNumChannels; ++ch)
            refBuffer.copyFrom (ch, capturePosition, playBuf, ch, 0, toWrite);

        // rec: mono = single return channel into both rec channels;
        //      stereo = Return pair as before.
        if (mono)
        {
            if (retCh < numInputChannels && inputChannelData[retCh] != nullptr)
            {
                recBuffer.copyFrom (0, capturePosition, inputChannelData[retCh], toWrite);
                recBuffer.copyFrom (1, capturePosition, inputChannelData[retCh], toWrite);
            }
            else
            {
                recBuffer.clear (0, capturePosition, toWrite);
                recBuffer.clear (1, capturePosition, toWrite);
            }
        }
        else
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const int srcCh = retCh + ch;
                if (srcCh < numInputChannels && inputChannelData[srcCh] != nullptr)
                    recBuffer.copyFrom (ch, capturePosition, inputChannelData[srcCh], toWrite);
                else
                    recBuffer.clear (ch, capturePosition, toWrite);
            }
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
void AudioEngine::trimRecBuffer (float lagSamples)
{
    const int intLag = (int) lagSamples;
    const float fracLag = lagSamples - (float) intLag;

    // --- Integer trim ---
    if (intLag > 0 && intLag < capturePosition)
    {
        const int newLength = capturePosition - intLag;

        for (int ch = 0; ch < recBuffer.getNumChannels(); ++ch)
        {
            float* data = recBuffer.getWritePointer (ch);
            std::memmove (data, data + intLag, sizeof (float) * (size_t) newLength);
        }

        capturePosition = newLength;
    }

    // --- Sub-sample fractional correction ---
    // Advances rec by fracLag samples using 4-point Lagrange interpolation so
    // that the on-disk WAV is aligned to sub-sample precision.
    LatencyAligner::applyFractionalDelay (recBuffer, capturePosition, fracLag);
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

        auto writer = std::unique_ptr<juce::AudioFormatWriter> (
            wav.createWriterFor (refStream,
                                 juce::AudioFormatWriterOptions{}
                                     .withSampleRate    (sampleRate)
                                     .withNumChannels   (refBuffer.getNumChannels())
                                     .withBitsPerSample (24)));

        if (writer == nullptr)
            return "Could not create WAV writer for ref file.";

        writer->writeFromAudioSampleBuffer (refBuffer, 0, capturePosition);
    }   // writer destroyed here → flushes + finalises WAV header

    // --- Write rec.wav ---
    {
        std::unique_ptr<juce::OutputStream> recStream = recFilePath.createOutputStream();

        if (recStream == nullptr)
            return "Could not create file: " + recFilePath.getFullPathName();

        auto writer = std::unique_ptr<juce::AudioFormatWriter> (
            wav.createWriterFor (recStream,
                                 juce::AudioFormatWriterOptions{}
                                     .withSampleRate    (sampleRate)
                                     .withNumChannels   (recBuffer.getNumChannels())
                                     .withBitsPerSample (24)));

        if (writer == nullptr)
            return "Could not create WAV writer for rec file.";

        writer->writeFromAudioSampleBuffer (recBuffer, 0, capturePosition);
    }   // writer destroyed here → flushes + finalises WAV header

    return {};   // success
}

//==============================================================================
juce::File AudioEngine::getSettingsFile()
{
    // ~/Library/Application Support/HardwareProfiler/audioSettings.xml
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support")
               .getChildFile ("HardwareProfiler")
               .getChildFile ("audioSettings.xml");
}

void AudioEngine::saveDeviceSettings() const
{
    auto xml = deviceManager.createStateXml();
    if (xml == nullptr) return;

    const juce::File file = getSettingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (xml->toString());
}


juce::String AudioEngine::getDeviceStatusString() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "No audio device";

    const int sr = (int) device->getCurrentSampleRate();
    const int bs = device->getCurrentBufferSizeSamples();
    return device->getName() + "  |  " + juce::String (sr) + " Hz  |  " + juce::String (bs);
}
