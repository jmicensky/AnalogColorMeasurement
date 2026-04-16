// Serializes and deserializes an LNLModel to/from a pretty-printed JSON artifact file.
// Handles both single-model (legacy) and multi-model (gainModels array) artifact formats for backward compatibility.

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
    root->setProperty ("inputPadDb", (double) model.inputPadDb);
    root->setProperty ("modelType",  model.modelType == LNLModel::ModelType::Volterra
                                         ? "Volterra" : "LNL");
    root->setProperty ("l1Fir",      vecToVar (model.l1Fir));
    root->setProperty ("l2Fir",      vecToVar (model.l2Fir));
    root->setProperty ("waveshaper", vecToVar (model.waveshaper));

    // Volterra kernels — only written for Volterra artifacts.
    if (model.modelType == LNLModel::ModelType::Volterra)
    {
        root->setProperty ("volterraM1", model.volterraM1);
        root->setProperty ("volterraM2", model.volterraM2);
        root->setProperty ("volterraH1", vecToVar (model.volterraH1));
        root->setProperty ("volterraH2", vecToVar (model.volterraH2));
    }
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

    // Per-gain-level models (absent in single-model artifacts).
    if (! model.gainModels.empty())
    {
        juce::Array<juce::var> gmArr;
        for (const auto& gm : model.gainModels)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("gainLabel",  gm.gainLabel);
            obj->setProperty ("gainValue",  (double) gm.gainValue);
            obj->setProperty ("waveshaper", vecToVar (gm.waveshaper));
            gmArr.add (juce::var (obj));
        }
        root->setProperty ("gainModels", gmArr);
    }

    // Right-channel fields (stereo devices only — absent for mono artifacts).
    if (model.isStereoModel())
    {
        root->setProperty ("l1FirR",         vecToVar (model.l1FirR));
        root->setProperty ("frMagnitudeDbR", vecToVar (model.frMagnitudeDbR));
        root->setProperty ("waveshaperR",    vecToVar (model.waveshaperR));
        if (! model.volterraH1R.empty())
        {
            root->setProperty ("volterraH1R", vecToVar (model.volterraH1R));
            root->setProperty ("volterraH2R", vecToVar (model.volterraH2R));
        }
        if (! model.gainModelsR.empty())
        {
            juce::Array<juce::var> gmArrR;
            for (const auto& gm : model.gainModelsR)
            {
                auto* obj = new juce::DynamicObject();
                obj->setProperty ("gainLabel",  gm.gainLabel);
                obj->setProperty ("gainValue",  (double) gm.gainValue);
                obj->setProperty ("waveshaper", vecToVar (gm.waveshaper));
                gmArrR.add (juce::var (obj));
            }
            root->setProperty ("gainModelsR", gmArrR);
        }
    }

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
    // Default 0.0 preserves existing behaviour for artifacts that pre-date this field.
    model.inputPadDb = parsed.hasProperty ("inputPadDb")
                           ? (float)(double) parsed["inputPadDb"] : 0.0f;
    // modelType defaults to LNL for all artifacts that pre-date the Volterra field.
    model.modelType  = (parsed["modelType"].toString() == "Volterra")
                           ? LNLModel::ModelType::Volterra
                           : LNLModel::ModelType::LNL;
    model.l1Fir      = varToVec (parsed["l1Fir"]);
    model.l2Fir      = varToVec (parsed["l2Fir"]);
    model.waveshaper = varToVec (parsed["waveshaper"]);

    // Volterra kernels — absent in LNL artifacts; defaults leave vectors empty.
    if (model.modelType == LNLModel::ModelType::Volterra)
    {
        model.volterraM1 = parsed.hasProperty ("volterraM1")
                               ? (int)(int64_t) parsed["volterraM1"] : 0;
        model.volterraM2 = parsed.hasProperty ("volterraM2")
                               ? (int)(int64_t) parsed["volterraM2"] : 0;
        model.volterraH1 = varToVec (parsed["volterraH1"]);
        model.volterraH2 = varToVec (parsed["volterraH2"]);
    }
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

    model.gainModels.clear();
    if (auto* gmArr = parsed["gainModels"].getArray())
    {
        for (const auto& item : *gmArr)
        {
            GainModel gm;
            gm.gainLabel  = item["gainLabel"].toString();
            gm.gainValue  = (float)(double) item["gainValue"];
            gm.waveshaper = varToVec (item["waveshaper"]);
            model.gainModels.push_back (std::move (gm));
        }
    }

    // Right-channel fields — absent in mono/legacy artifacts, default to empty.
    model.l1FirR         = varToVec (parsed["l1FirR"]);
    model.frMagnitudeDbR = varToVec (parsed["frMagnitudeDbR"]);
    model.waveshaperR    = varToVec (parsed["waveshaperR"]);
    model.volterraH1R    = varToVec (parsed["volterraH1R"]);
    model.volterraH2R    = varToVec (parsed["volterraH2R"]);
    model.gainModelsR.clear();
    if (auto* gmArrR = parsed["gainModelsR"].getArray())
    {
        for (const auto& item : *gmArrR)
        {
            GainModel gm;
            gm.gainLabel  = item["gainLabel"].toString();
            gm.gainValue  = (float)(double) item["gainValue"];
            gm.waveshaper = varToVec (item["waveshaper"]);
            model.gainModelsR.push_back (std::move (gm));
        }
    }

    if (! model.isValid())
        return model.modelType == LNLModel::ModelType::Volterra
                   ? "Artifact file is missing required Volterra fields (volterraH1/H2, waveshaper)."
                   : "Artifact file is missing required fields (l1Fir, waveshaper).";

    return {};
}
