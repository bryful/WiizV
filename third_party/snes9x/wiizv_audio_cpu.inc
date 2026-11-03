extern "C" void wiizv_audio_transfer(bool transferring);

static inline void wiizvAudioTransferHook()
{
    if (Memory.ROMCRC32 != 0x547cf7b9) return;
    const auto pc = Registers.PBPC;
    // 共通のポート応答待ちでは、呼出元で設定した転送状態を維持する。
    if (pc >= 0x0a8201 && pc <= 0x0a8209) return;
    wiizv_audio_transfer((pc >= 0x0a8097 && pc <= 0x0a815d) ||
                         (pc >= 0x0a816a && pc <= 0x0a81ff));
}
