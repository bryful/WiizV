// 探索の入力待ちからだけ、既存の階層再読込を伴う移動処理へ入る。
static bool wiizvMovePending = false;
static unsigned wiizvMoveStatus = 0;
static uint8 wiizvMoveX, wiizvMoveY, wiizvMoveFloor;

static bool wiizvEventWait(unsigned waitStack)
{
    if (Memory.RAM[0x1fde] != 0xc1 || Memory.RAM[0x1fdf] != 0x80)
        return false;
    // 探索のマスイベントから呼ばれた選択肢、または直接のメッセージ待ち。
    return (waitStack == 0x1fd1 && Memory.RAM[0x1fd6] == 0x86 &&
        Memory.RAM[0x1fd7] == 0x9d && Memory.RAM[0x1fd8] == 0 &&
        Memory.RAM[0x1fd9] == 0x47 && Memory.RAM[0x1fda] == 0x9d && Memory.RAM[0x1fdd] == 5) ||
        (waitStack == 0x1fd6 && Memory.RAM[0x1fdd] == 5);
}

extern "C" WIIZV_EXPORT bool wiizv_can_move()
{
    if (Registers.PB != 0 || Memory.RAM[0xdb] == 0 || Memory.RAM[0xdb] > 6 ||
        Memory.RAM[0xb7] < 1 || Memory.RAM[0xb7] > 8)
        return false;
    const unsigned waitStack = Memory.RAM[0x1a] | (Memory.RAM[0x1b] << 8);
    const bool waiting = (Registers.PCw >= 0x8215 && Registers.PCw <= 0x8218 && Registers.S.W == waitStack) ||
        (Registers.PCw == 0x8150 && Registers.S.W + 4u == waitStack);
    const bool exploring = waitStack == 0x1fd8 && Memory.RAM[0x1fdd] == 0x12 &&
        Memory.RAM[0x1fde] == 0x81 && Memory.RAM[0x1fdf] == 5;
    return waiting && (exploring || wiizvEventWait(waitStack));
}

extern "C" WIIZV_EXPORT bool wiizv_request_move(int x, int y, unsigned floor)
{
    if (!wiizv_can_move() || x < -128 || x > 127 || y < -128 || y > 127 || floor < 1 || floor > 8)
        return false;
    wiizvMoveX = static_cast<uint8>(x);
    wiizvMoveY = static_cast<uint8>(y);
    wiizvMoveFloor = static_cast<uint8>(floor);
    wiizvMovePending = true;
    wiizvMoveStatus = 1;
    return true;
}

extern "C" WIIZV_EXPORT void wiizv_cancel_move()
{
    wiizvMovePending = false;
    wiizvMoveStatus = 0;
}

extern "C" WIIZV_EXPORT unsigned wiizv_move_status() { return wiizvMoveStatus; }

static inline void wiizvMovementHook()
{
    if (wiizvMoveStatus == 3 && Registers.PB == 5 && Registers.PCw == 0x809f)
        wiizvMoveStatus = 2;
    if (!wiizvMovePending) return;
    const unsigned waitStack = Memory.RAM[0x1a] | (Memory.RAM[0x1b] << 8);
    const bool eventReturn = Registers.PB == 0 && Registers.PCw == 0x81f5 &&
        Registers.S.W == waitStack + 4 && wiizvEventWait(waitStack) &&
        Registers.DB == 0 && Registers.D.W == 0 && CheckMemory() && !CheckIndex();
    if (wiizvMovePending && ((Registers.PB == 5 && Registers.PCw == 0x8113) || eventReturn))
    {
        wiizvMovePending = false;
        wiizvMoveStatus = 3;
        Memory.RAM[0xf0] = wiizvMoveX;
        Memory.RAM[0xf1] = wiizvMoveY;
        Memory.RAM[0xf2] = wiizvMoveFloor;
        if (eventReturn)
        {
            // NMI待ちがレジスタを戻した後にイベントを抜け、既存処理で確認表示を消す。
            Registers.S.W = 0x1fdc;
            Memory.RAM[0x1fdd] = 0xa3;
            Memory.RAM[0x1fde] = 0xad;
            Memory.RAM[0x1fdf] = 5;
            S9xSetPCBase(0x008fe7);
        }
        else S9xSetPCBase(0x05ada4);
    }
}
