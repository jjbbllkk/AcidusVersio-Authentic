#include "daisy_versio.h"
#include "daisysp.h"
#include "rosic_Open303.h"
#include "Note.hpp"
#include <new>

using namespace daisy;
using namespace daisysp;
using namespace rosic;

DaisyVersio hw;

// Force 16-byte alignment to prevent crashes with doubles/floats
uint8_t DSY_SDRAM_BSS __attribute__((aligned(16))) tb303_memory[sizeof(Open303)];
Open303 *tb303_ptr;
#define tb303 (*tb303_ptr)

enum param_mode {
    BABYFISH,
    NORMAL,
    DEVILFISH
};

uint8_t mode  = BABYFISH;
float sampleRate;

// LOGIC FIX: Changed prevTrigger to bool for proper logic checks
bool prevTrigger = false; 

float tuning;
bool slideToNextNote;
int active_note = 60; // Keep track of which note is playing so we can turn it off

bool inCalibration;
const int calibration_max = 65536;
const int calibration_min = 63200;
uint16_t calibration_Offset = 64262;
uint16_t calibration_UnitsPerVolt = 12826;
uint16_t calibration_UnitsPerVolt_ADC = 12826;
uint16_t calibration_UnitsPerVolt_DAC = 0;

const uint16_t calibration_thresh = calibration_max - 200;
const uint16_t base_octave = 2;
const uint16_t max_midi_note = (12*(base_octave + 6)); 
const uint16_t min_midi_note = (base_octave * 12);

// Persistence
struct Settings {
    float calibration_Offset;
    float calibration_UnitsPerVolt;
    bool operator!=(const Settings& a) {
        return a.calibration_UnitsPerVolt != calibration_UnitsPerVolt;
    }
};
Settings& operator* (const Settings& settings) { return *settings; }
PersistentStorage<Settings> storage(hw.seed.qspi);

void saveData() {
    Settings &localSettings = storage.GetSettings();
    localSettings.calibration_Offset = calibration_Offset;
    localSettings.calibration_UnitsPerVolt = calibration_UnitsPerVolt;
    storage.Save();
}

void loadData() {
    Settings &localSettings = storage.GetSettings();
    calibration_Offset = localSettings.calibration_Offset;
    calibration_UnitsPerVolt = localSettings.calibration_UnitsPerVolt;
}

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    float sample = 0.0;
    for(size_t i = 0; i < size; i += 1)
    {
        sample = (float) tb303.getSample();

        OUT_L[i] = sample;
        OUT_R[i] = sample;

    }
    // Set lights on right side, yellowish
    hw.SetLed(hw.LED_2,sample*2,sample*2,0);
    hw.SetLed(hw.LED_3,sample*2,sample*2,0);
    hw.UpdateLeds();

}

void doTriggerReceived() {

    // --- FIX 1: CV LAG / SETTLE TIME ---
    // Wait 4ms for the voltage to stabilize in the capacitors.
    // This prevents reading the "previous" note (octave lag) or "in-between" voltages (random notes).
    System::Delay(4);
    hw.ProcessAllControls(); 
    // -----------------------------------

    // Convert CV from tuning jack to midi note number
    Note currentNote = Note();
    float raw_cv = hw.knobs[hw.KNOB_0].GetRawValue();
    float volts;
    if (raw_cv > calibration_min) {
        volts = 0.0;
    } else {
        volts = DSY_CLAMP((calibration_Offset - raw_cv)/calibration_UnitsPerVolt,0,5.0);
    }
    float midi_pitch = round(((volts) * 12) + (12 * (base_octave + 1)));
    if (midi_pitch > max_midi_note) midi_pitch = max_midi_note;
    if (midi_pitch < min_midi_note) midi_pitch = min_midi_note;
    
    // --- FIX 2: SLIDE / PORTAMENTO ---
    // 1. Read the knob (0.0 to 1.0)
    float slide_val = hw.GetKnobValue(DaisyVersio::KNOB_3);
    
    // 2. Determine if Slide is Active (Threshold > 5%)
    bool is_sliding = (slide_val > 0.05f);

    // 3. Map Knob to Slide Time (Slew Amount)
    //    Range: 50ms (fast) to 400ms (slow drag)
    if (is_sliding) {
        tb303.setSlideTime(slide_val * 400.0f);
    } else {
        tb303.setSlideTime(60.0f); // Default 303 speed
    }

    active_note = (int)midi_pitch;

    // --- FIX 3: ACCENT "ATTENUATOR" STYLE ---
    // Threshold set to 0.9 (90%).
    // Knob (0-90%): Controls "Intensity" (Filter/Res boost) but does not trigger the snap.
    // Gate Input (or Knob > 90%): Triggers the velocity 127 "Snap".
    float accent_val = hw.GetKnobValue(DaisyVersio::KNOB_6);
    int velocity = (accent_val > 0.9f) ? 127 : 100;

    // --- TRIGGER EXECUTION ---
    if (!is_sliding) {
        // STANDARD TRIGGER
        // Stop previous notes, jump to new pitch.
        tb303.allNotesOff();
        tb303.noteOn(active_note, velocity);
    } else {
        // SLIDE / PORTAMENTO TRIGGER
        // Keep previous note frequency, glide to new pitch.
        tb303.trimNoteList();
        
        // Use helper function to Glide + Trigger Envelope safely
        tb303.noteOnPortamento(active_note, velocity);
    }
}

void doNoteOff() {
    // ENVELOPE FIX: Send a velocity 0 message to trigger the Release phase
    tb303.noteOn(active_note, 0);
}

void waitForButton() {
    while (!hw.tap.RisingEdge()) {
        hw.tap.Debounce();
    }
    while (!hw.tap.FallingEdge()) {
        hw.tap.Debounce();
    }
    System::Delay(200);
}


void doCalibration() {

    inCalibration = true;
    uint8_t numSamples = 10; // Take this many samples per step

    // STEP ONE - RELEASE BUTTON
    hw.tap.Debounce();
    hw.SetLed(0,1,1,1);
    hw.SetLed(1,1,1,1);
    hw.SetLed(2,1,1,1);
    hw.SetLed(3,1,1,1); 
    hw.UpdateLeds();
    while (hw.tap.RawState()) {
        hw.tap.Debounce();
    }
    hw.SetLed(0,0,1,0);
    hw.SetLed(1,0,0,0);
    hw.SetLed(2,0,0,0);
    hw.SetLed(3,0,0,0);
    hw.UpdateLeds();
    waitForButton();
    float onevolt_value = hw.knobs[0].GetRawValue();

    // STEP TWO - TWO VOLTS
    hw.SetLed(0,0,0,1);
    hw.SetLed(1,0,0,1);
    hw.SetLed(2,0,0,0);
    hw.SetLed(3,0,0,0);
    hw.UpdateLeds();
    waitForButton();
    float total = 0;
    for (uint8_t x = 0; x < numSamples; ++x) {
        hw.knobs[0].Process();
        total = total + hw.knobs[0].GetRawValue();
    }
    float twovolt_value = total/numSamples;

    // STEP TWO - ONE THREE VOLTS
    hw.SetLed(0,0,1,1);
    hw.SetLed(1,0,1,1);
    hw.SetLed(2,0,1,1);
    hw.SetLed(3,0,0,0);
    hw.UpdateLeds();
    waitForButton();
    
    total = 0;
    for (uint8_t x = 0; x < numSamples; ++x) {
        hw.knobs[0].Process();
        total = total + hw.knobs[0].GetRawValue();
    }
    float threevolt_value = total/numSamples;

    // Estimate calibration values
    uint16_t first_estimate = (float)(onevolt_value - twovolt_value );
    float second_estimate = (float)(twovolt_value - threevolt_value );
    float avg_estimate = (first_estimate + second_estimate)/2;
    uint16_t offset = onevolt_value + avg_estimate;

    // Save values to QSPI
    calibration_Offset = offset;
    calibration_UnitsPerVolt = round(avg_estimate);
    hw.seed.PrintLine("Offset %d upv %d",calibration_Offset,calibration_UnitsPerVolt);
    saveData();

    inCalibration = false;

}

int main(void)
{

    // Initialize Versio hardware and start audio, ADC
    hw.Init();

    // --- MEMORY FIX: Initialize SDRAM object ---
    tb303_ptr = new(tb303_memory) Open303();

    hw.seed.StartLog(false);
    hw.StartAdc();

    hw.SetLed(0,1,1,1);
    hw.SetLed(1,1,1,1);
    hw.SetLed(2,1,1,1);
    hw.SetLed(3,1,1,1);
    hw.UpdateLeds();

    sampleRate = hw.seed.AudioSampleRate();

    hw.StartAudio(AudioCallback);

    Settings defaults;
    defaults.calibration_Offset = calibration_Offset;
    defaults.calibration_UnitsPerVolt = calibration_UnitsPerVolt;
    storage.Init(defaults);

    loadData();

    // If we don't have good saved data, revert to defaults
    if (calibration_UnitsPerVolt < 400 || calibration_UnitsPerVolt > 20000) {
        storage.RestoreDefaults();  
        loadData();
    }

    // Set up 303
    tb303.setSampleRate(sampleRate);
    tb303.setVolume(0);
    
    // AUTHENTICITY FIX: 
    // 1. Set Volume Envelope to ~4 seconds (Original 303 spec) so held notes sustain longer.
    tb303.setAmpDecay(4000); 
    
    // 2. Set Volume Release to 15ms (prevent clicking when you let go of the button)
    tb303.setAmpRelease(15); 
    
    tb303.setDecay(500); // Default Start Value

    // Check for calibration routine
    hw.ProcessAllControls();
    hw.tap.Debounce();
    if (hw.sw[0].Read() == hw.sw->POS_RIGHT && hw.sw[1].Read() == hw.sw->POS_RIGHT) {
        if (hw.tap.RawState()) { 
            doCalibration();
        }
    }

    while(1) {

        hw.ProcessAllControls();
        hw.tap.Debounce();
        hw.UpdateLeds();

        // Read knob values
        float decay     = hw.GetKnobValue(DaisyVersio::KNOB_1);
        float cutoff    = hw.GetKnobValue(DaisyVersio::KNOB_2);
        float slide     = DSY_CLAMP(hw.GetKnobValue(DaisyVersio::KNOB_3),0,0.5);
        float resonance = hw.GetKnobValue(DaisyVersio::KNOB_4);
        float envmod    = hw.GetKnobValue(DaisyVersio::KNOB_5);
        float accent    = hw.GetKnobValue(DaisyVersio::KNOB_6);

// Map knob values onto parameters depending on mode
        switch (mode) {
            case BABYFISH:
                // Babyfish: Tame range, knob controls both normal and accent decay
                tb303.setDecay(decay * 1000 + 200); 
                tb303.setAccentDecay(decay * 1000 + 200);
                
                tb303.setResonance(resonance * 80 + 10);
                tb303.setCutoff(cutoff * 4000);
                tb303.setAccent(accent * 40);
                tb303.setEnvMod(envmod * 80 + 10);
            break;

            case NORMAL:
                // NORMAL 303 BEHAVIOR:
                // 1. Decay Knob controls Filter Decay (200ms to 2000ms range)
                tb303.setDecay(decay * 1800 + 200); 

                // 2. Authenticity: Accents on a real 303 have a FIXED fast decay (knob is ignored!)
                tb303.setAccentDecay(200); 

                tb303.setResonance(resonance * 90);
                tb303.setCutoff(cutoff * 5000);
                tb303.setAccent(accent * 50);
                tb303.setEnvMod(envmod * 100);
            break;

            case DEVILFISH:
                // DEVILFISH MOD BEHAVIOR:
                // 1. Huge Range (30ms to 3000ms)
                tb303.setDecay(decay * 3000 + 30);
                
                // 2. Devilfish mod connects the Knob to Accent Decay too!
                tb303.setAccentDecay(decay * 3000 + 30);

                tb303.setResonance(resonance * 100);
                tb303.setCutoff(cutoff * 10000);
                tb303.setAccent(accent * 100);
                tb303.setEnvMod(envmod * 100);
            break;
        }

        // Activate slide
        if (slide > 0.1) {
            slideToNextNote = true;
        } else {
            slideToNextNote = false;
        }

        // Read in trigger, comes from FSU or button
        bool triggerIn = hw.Gate();

        // LOGIC FIX: Check for Rising Edge properly
        if (triggerIn && !prevTrigger) {
            doTriggerReceived();
        }

        // LOGIC FIX: Check for Falling Edge (Note Off)
        if (!triggerIn && prevTrigger) {
            doNoteOff();
        }
        
        // LOGIC FIX: Update history
        prevTrigger = triggerIn;


        if(hw.tap.RisingEdge()) {
            doTriggerReceived();
        }
        // Manual button release handled by gate logic above if HW Tap is tied to gate?
        // Usually Tap is separate. Let's add explicit Tap release just in case.
        if(hw.tap.FallingEdge()) {
            doNoteOff();
        }

        // Top switch
         if (hw.sw[0].Read() == hw.sw->POS_LEFT) {
                tb303.setWaveform(0);
         } else if (hw.sw[0].Read() == hw.sw->POS_CENTER) {
                tb303.setWaveform(0.5);
         } else {
                tb303.setWaveform(1.0);
         }

        // Bottom switch
         if (hw.sw[1].Read() == hw.sw->POS_LEFT) {
                mode = BABYFISH;
         } else if (hw.sw[1].Read() == hw.sw->POS_CENTER) {
                mode = NORMAL;
         } else {
                mode = DEVILFISH;
         }
        // Set left side lights, orangish
        if (!inCalibration) {
            // float env = tb303.mainEnv.getSample(); // Unused
            hw.SetLed(hw.LED_0,triggerIn,triggerIn/1.7,0);
            hw.SetLed(hw.LED_1,triggerIn,triggerIn/1.7,0);
            hw.UpdateLeds();
        }
    }
}