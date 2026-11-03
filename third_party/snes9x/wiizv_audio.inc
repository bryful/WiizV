// WiizV: run the SMP/DSP at normal wall-clock speed while the main CPU runs faster.
// Included by apu/apu.cpp; original implementation remains under the Snes9x license.
static uint32 wiizvSpeedNumerator = 1;
static uint32 wiizvSpeedDenominator = 1;
static unsigned wiizvRequestedNumerator = 1, wiizvRequestedDenominator = 1;
static bool wiizvAudioTransfer = false;

#ifdef _WIN32
#define WIIZV_EXPORT __declspec(dllexport)
#else
#define WIIZV_EXPORT __attribute__((visibility("default")))
#endif

static bool wiizvChangeAudioSpeed(unsigned numerator, unsigned denominator)
{
    if (!((numerator == 1 && denominator == 1) || (numerator == 3 && denominator == 2) ||
          (numerator == 2 && denominator == 1) || (numerator == 3 && denominator == 1) ||
          (numerator == 4 && denominator == 1)))
        return false;
    if (numerator == wiizvSpeedNumerator && denominator == wiizvSpeedDenominator)
        return true;
    S9xAPUExecute();
    // Preserve the fractional SMP clock when changing the denominator.
    const uint64 oldDenominator = static_cast<uint64>(spc::ratio_denominator) * wiizvSpeedNumerator;
    const uint64 newDenominator = static_cast<uint64>(spc::ratio_denominator) * numerator;
    spc::remainder = static_cast<uint32>(static_cast<uint64>(spc::remainder) * newDenominator / oldDenominator);
    wiizvSpeedNumerator = numerator;
    wiizvSpeedDenominator = denominator;
    return true;
}

extern "C" WIIZV_EXPORT bool wiizv_set_audio_speed(unsigned numerator, unsigned denominator)
{
    if (!((numerator == 1 && denominator == 1) || (numerator == 3 && denominator == 2) ||
          (numerator == 2 && denominator == 1) || (numerator == 3 && denominator == 1) ||
          (numerator == 4 && denominator == 1))) return false;
    wiizvRequestedNumerator = numerator;
    wiizvRequestedDenominator = denominator;
    return wiizvChangeAudioSpeed(wiizvAudioTransfer ? 1 : numerator, wiizvAudioTransfer ? 1 : denominator);
}

extern "C" void wiizv_audio_transfer(bool transferring)
{
    if (wiizvAudioTransfer == transferring) return;
    // SPCへの転送中は、連続するポート書込みを取りこぼさない本来のクロック比に戻す。
    wiizvChangeAudioSpeed(transferring ? 1 : wiizvRequestedNumerator,
                          transferring ? 1 : wiizvRequestedDenominator);
    wiizvAudioTransfer = transferring;
}
