#include "ArtifactFile.h"

//==============================================================================
// Helpers
//==============================================================================
static juce::Array<juce::var> vecToVar (const std::vector<float>& v)
{
    juce::Array<juce::var> arr;
    arr.resize ((int) v.size());
    for (int i = 0; i < (int) v.size(); ++i)
        arr.set (i, (double) v[i]);
    return arr;
}

static std::vector<float> varToVec (const juce::var& v)
{
    std::vector<float> result;
    if (auto* arr = v.getArray())
    {
        result.resize ((size_t) arr->size());
        for (int i = 0; i < arr->size(); ++i)
            result[(size_t) i] = (float)(double) (*arr)[i];
    }
    return result;
}

//==============================================================================
juce::String ArtifactFile::save (const LNLModel& model, const juce::File& file)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("version",    model.version);
    root->setProperty ("deviceName", model.deviceName);
    root->setProperty ("date",       model.date);
    root->setProperty ("sampleRate", model.sampleRate);
    root->setProperty ("l1Fir",      vecToVar (model.l1Fir));
    root->setProperty ("l2Fir",      vecToVar (model.l2Fir));
    root->setProperty ("waveshaper", vecToVar (model.waveshaper));
    root->setProperty ("frFrequencies", vecToVar (model.frFrequencies));
    root->setProperty ("frMagnitudeDb", vecToVar (model.frMagnitudeDb));

    juce::Array<juce::var> thdArr;
    for (const auto& t : model.thdResults)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("gainLabel",              t.gainLabel);
        obj->setProperty ("stimulusName",           t.stimulusName);
        obj->setProperty ("thdPercent",             (double) t.thdPercent);
        obj->setProperty ("fundamentalAmplitudeDb", (double) t.fundamentalAmplitudeDb);
        thdArr.add (juce::var (obj));
    }
    root->setProperty ("thdResults", thdArr);

    const juce::String json = juce::JSON::toString (juce::var (root), true);

    if (! file.replaceWithText (json))
        return "Could not write artifact file: " + file.getFullPathName();

    return {};
}

//==============================================================================
juce::String ArtifactFile::load (const juce::File& file, LNLModel& model)
{
    if (! file.existsAsFile())
        return "Artifact file not found: " + file.getFullPathName();

    juce::var parsed;
    const juce::Result r = juce::JSON::parse (file.loadFileAsString(), parsed);
    if (r.failed())
        return "JSON parse error: " + r.getErrorMessage();

    model.version    = (int)(int64_t) parsed["version"];
    model.deviceName = parsed["deviceName"].toString();
    model.date       = parsed["date"].toString();
    model.sampleRate = (double) parsed["sampleRate"];
    model.l1Fir      = varToVec (parsed["l1Fir"]);
    model.l2Fir      = varToVec (parsed["l2Fir"]);
    model.waveshaper = varToVec (parsed["waveshaper"]);
    model.frFrequencies = varToVec (parsed["frFrequencies"]);
    model.frMagnitudeDb = varToVec (parsed["frMagnitudeDb"]);

    model.thdResults.clear();
    if (auto* arr = parsed["thdResults"].getArray())
    {
        for (const auto& item : *arr)
        {
            THDEntry e;
            e.gainLabel              = item["gainLabel"].toString();
            e.stimulusName           = item["stimulusName"].toString();
            e.thdPercent             = (float)(double) item["thdPercent"];
            e.fundamentalAmplitudeDb = (float)(double) item["fundamentalAmplitudeDb"];
            model.thdResults.push_back (e);
        }
    }

    if (! model.isValid())
        return "Artifact file is missing required fields (l1Fir, waveshaper).";

    return {};
}
