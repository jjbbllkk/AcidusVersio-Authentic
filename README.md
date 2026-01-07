# Acidus Versio (Authentic 303 Mod)

This is a heavily modified version of the AcidusVersio firmware. It moves the Open303 engine to the Daisy's external SDRAM (allowing for the full engine to run without crashing) and rewrites the control logic to match the physical behavior of a real Roland TB-303.

## Key Changes in this Version
* **SDRAM Architecture:** The engine now runs in the 64MB external SDRAM, fixing the "Flash Overflow" errors and allowing for future expansion.
* **Authentic Envelopes:** * Volume Envelope extended to ~4 seconds (matching original 303 spec).
    * Filter Decay ranges corrected (Normal: 200ms-2s, Devilfish: 30ms-3s).
    * Accents now trigger a fixed, snappy decay time (ignoring the knob), just like the hardware.
* **Accent Thresholding:** Fixed the "Always On" accent bug. The Accent Knob now acts as an intensity control, while the Accent Gate input (or turning the knob past 90%) triggers the actual "snap" velocity.
* **Variable Slide:** Slide knob now controls the Portamento time (Slew Rate), allowing for slow drags or fast slides.
* **Crash Fixes:** Added 16-byte memory alignment to prevent Hard Faults on startup.

## How to Flash (IMPORTANT)
**The flashing process has changed.** Because this firmware uses the external SDRAM, you must use the **Daisy Bootloader**, not the standard System DFU.

### 1. Install the Bootloader (Do this once)
1.  Connect Versio via USB.
2.  Enter System Mode: Hold **BOOT**, press **RESET**, release **BOOT**.
3.  Run:
    ```bash
    make program-boot
    ```
4.  The module will reboot and the LED will start "breathing" (fading in and out).

### 2. Flash the Firmware
**Do not** press BOOT+RESET. 
1.  Ensure the LED is "breathing" (if not, tap RESET once).
2.  Run:
    ```bash
    make program-dfu
    ```

## Controls
| Knob | Function | Note |
| :--- | :--- | :--- |
| **1** | Decay | Normal: Filter Decay. Devilfish: Filter + Accent Decay. |
| **2** | Cutoff | Filter Frequency. |
| **3** | Slide | Turn CW to increase Slide Time (Slew). |
| **4** | Resonance | Filter Q. |
| **5** | Env Mod | Filter Envelope Depth. |
| **6** | Accent | Intensity. Turn past 3 o'clock to manually trigger Accent. |

**Switches:**
* **Top:** Waveform (Saw / Mix / Square)
* **Bottom:** Mode (Babyfish / Normal / Devilfish)