#include "../Source/DSP/DualCabEngine.h"
#include <juce_events/juce_events.h>
#include <array>
#include <cmath>
#include <iostream>

namespace
{
int passed = 0, failed = 0;
void expect (bool ok, const char* message)
{
    (ok ? passed : failed)++;
    if (! ok) std::cerr << "FAIL: " << message << '\n';
}
juce::AudioBuffer<float> ir (float first, float second = 0.0f)
{
    juce::AudioBuffer<float> result (1, 2);
    result.setSample (0, 0, first);
    result.setSample (0, 1, second);
    return result;
}
juce::AudioBuffer<float> impulse (int channels, int samples = 64)
{
    juce::AudioBuffer<float> result (channels, samples);
    result.clear();
    for (int c = 0; c < channels; ++c) result.setSample (c, 0, 1.0f);
    return result;
}
void response (const juce::AudioBuffer<float>& audio, float first, float second, const char* message)
{
    bool ok = true;
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int s = 0; s < audio.getNumSamples(); ++s)
        {
            const auto actual = audio.getSample (c, s);
            const auto expected = s == 0 ? first : (s == 1 ? second : 0.0f);
            ok = ok && std::isfinite (actual) && std::abs (actual - expected) < 1.0e-4f;
        }
    expect (ok, message);
}
void settle (dualir::DualCabEngine& engine, const dualir::EngineSettings& settings, int channels)
{
    juce::AudioBuffer<float> silence (channels, 64);
    for (int i = 0; i < 64; ++i) { silence.clear(); engine.process (silence, settings); }
}
void render (dualir::EngineSettings settings, bool a, bool b, float first, float second,
             const char* message, int channels = 2)
{
    dualir::DualCabEngine engine;
    if (a) engine.queueImpulse (0, ir (0.5f, 0.25f), 48000.0);
    if (b) engine.queueImpulse (1, ir (0.25f, -0.125f), 48000.0);
    engine.prepare (48000.0, 64, channels);
    settle (engine, settings, channels);
    auto audio = impulse (channels);
    engine.process (audio, settings);
    const auto calibration = settings.bypass ? 1.0f : std::pow (10.0f, -12.0f / 20.0f);
    response (audio, first * calibration, second * calibration, message);
}
}
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    for (int channels : { 1, 2 })
    {
        dualir::CabProcessor cab;
        cab.queueImpulse (ir (0.5f, 0.25f), 48000.0);
        cab.prepare (48000.0, 64, channels);
        expect (cab.hasImpulse(), "IR queued before prepare becomes ready synchronously");
        auto audio = impulse (channels);
        cab.process (audio);
        response (audio, 0.5f, 0.25f, "Exact two-tap FIR in mono/stereo");
        cab.reset(); audio.clear(); cab.process (audio);
        response (audio, 0.0f, 0.0f, "Reset clears history");
    }
    {
        dualir::CabProcessor cab;
        juce::AudioBuffer<float> stereo (2, 2);
        stereo.setSample (0, 0, 0.5f); stereo.setSample (0, 1, 0.25f);
        stereo.setSample (1, 0, 0.25f); stereo.setSample (1, 1, -0.125f);
        cab.queueImpulse (std::move (stereo), 48000.0); cab.prepare (48000.0, 64, 2);
        auto audio = impulse (2); audio.clear (1, 0, 64); cab.process (audio);
        expect (std::abs (audio.getSample (0, 0) - 0.5f) < 1.0e-4f
                && audio.getMagnitude (1, 0, 64) < 1.0e-5f, "Stereo channels do not crossfeed");
    }
    {
        dualir::CabProcessor cab;
        juce::AudioBuffer<float> source (1, 882); source.clear(); source.setSample (0, 441, 1.0f);
        cab.queueImpulse (std::move (source), 44100.0); cab.prepare (48000.0, 1024, 1);
        auto audio = impulse (1, 1024); cab.process (audio);
        int peak = 0;
        for (int i = 1; i < 1024; ++i)
            if (std::abs (audio.getSample (0, i)) > std::abs (audio.getSample (0, peak))) peak = i;
        expect (std::abs (peak - 480) <= 2 && std::abs (audio.getSample (0, peak)) > 0.2f,
                "Resampling preserves 10 ms impulse timing");
    }
    dualir::EngineSettings settings;
    render (settings, false, false, 1.0f, 0.0f, "No IR receives final -12 dB calibration");
    settings.blend = 0.95f;
    render (settings, true, false, 0.5f, 0.25f, "Only A has full weight; no dry leak");
    settings.blend = 0.05f;
    render (settings, false, true, 0.25f, -0.125f, "Only B has full weight; no dry leak");
    for (auto blend : std::array { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        settings.blend = blend;
        render (settings, true, true, (1.0f - blend) * 0.5f + blend * 0.25f,
                (1.0f - blend) * 0.25f - blend * 0.125f, "Both loaded: linear A/B mix");
    }
    settings = {}; settings.a.enabled = false; settings.b.enabled = false;
    render (settings, true, true, 0.0f, 0.0f, "Both muted are silent");
    settings = {}; settings.a.enabled = false; settings.blend = 0.25f;
    render (settings, true, true, 0.0625f, -0.03125f, "Muted A retains B blend weight");
    settings = {}; settings.a.gainDb = 6.0206f;
    render (settings, true, false, 1.0f, 0.5f, "Cab gain after smoothing");
    settings = {}; settings.outputDb = -6.0206f;
    render (settings, true, false, 0.25f, 0.125f, "Output gain after smoothing");
    settings = {}; settings.a.inverted = true;
    render (settings, true, false, -0.5f, -0.25f, "Mono polarity inversion", 1);
    settings = {}; settings.outputDb = -24.0f; settings.a.enabled = false; settings.bypass = true;
    render (settings, true, true, 1.0f, 0.0f, "Bypass ignores all wet controls");
    {
        dualir::DualCabEngine engine;
        engine.queueImpulse (0, ir (0.5f, 0.25f), 48000.0);
        engine.queueImpulse (1, ir (0.5f, 0.25f), 48000.0);
        engine.prepare (48000.0, 64, 2); settings = {}; settings.b.inverted = true;
        settle (engine, settings, 2); auto audio = impulse (2); engine.process (audio, settings);
        response (audio, 0.0f, 0.0f, "Identical IR opposite polarity cancel");
    }
    {
        dualir::DualCabEngine engine;
        engine.queueImpulse (0, ir (0.5f, 0.25f), 48000.0); engine.prepare (48000.0, 64, 2);
        settings = {}; settle (engine, settings, 2);
        auto audio = impulse (2, 257); audio.addSample (0, 256, 1.0f); audio.addSample (1, 256, 1.0f);
        engine.process (audio, settings);
        expect (std::abs (audio.getSample (0, 0) - 0.5f * std::pow (10.0f, -0.6f)) < 1.0e-4f
                && std::abs (audio.getSample (1, 256) - 0.5f * std::pow (10.0f, -0.6f)) < 1.0e-4f, "Oversized block chunking");
        audio.clear(); engine.process (audio, settings);
        response (audio, 0.25f * std::pow (10.0f, -0.6f), 0.0f, "Tail survives oversized block boundary");
    }
    std::cout << passed << '/' << passed + failed << " DSP checks passed\n";
    return failed == 0 ? 0 : 1;
}
