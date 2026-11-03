#pragma once

inline void runMovementChecks(Player& session)
{
    require(!session.dungeonMaps[0].empty(), "移動検証用ROMが一致しません。");
    session.setSpeed(0);
    session.replayInput = true;
    session.replayButtons = 0;
    auto wait = [&](int frames) { session.replayButtons = 0; runTestFrames(session, frames); };
    auto tap = [&](uint16_t buttons, int after = 100)
    {
        session.replayButtons = buttons; runTestFrames(session, 10); wait(after);
    };
    auto rejectedScene = [&]
    {
        const auto before = memoryHash(session);
        bool rejected = false;
        try { session.openMap(); } catch (const std::exception&) { rejected = true; }
        require(rejected && !session.mapOpen && before == memoryHash(session), "探索以外で移動画面が開きました。");
    };
    rejectedScene();
    tap(8, 240); tap(256, 590);
    rejectedScene();
    tap(256); tap(256); tap(256);
    for (int i = 0; i < 5; ++i) tap(256, 50);
    tap(1); tap(16, 40); tap(256, 80); tap(256, 300);
    rejectedScene(); // 入場直後のキャンプ
    tap(1);
    // 階段の確認中も探索として扱い、同階層・別階層へ移動できる。
    const auto stairState = encodeState(session.captureState(), session.romId, session.coreId);
    const auto stairRam = memoryHash(session);
    session.replayInput = false;
    session.inputBlocked = true;
    const bool oldSwap=session.swapButtons;
    for(bool swapped:{false,true}){
        session.swapButtons=swapped;session.applyStateFile(stairState);session.openMap();
        SDL_Event pad{};pad.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        pad.gbutton.button=swapped?SDL_GAMEPAD_BUTTON_EAST:SDL_GAMEPAD_BUTTON_SOUTH;
        handleAction(session,pad);require(!session.mapOpen,"AB交換設定に従って移動マップを閉じられません。");
        session.openMap();session.mapY=1;
        pad.gbutton.button=swapped?SDL_GAMEPAD_BUTTON_SOUTH:SDL_GAMEPAD_BUTTON_EAST;
        handleAction(session,pad);require(!session.mapOpen&&session.gameRam()[0xb6]==1,"AB交換設定に従って移動を決定できません。");
    }
    const bool oldFocus=session.focused;session.focused=true;
    for(bool swapped:{false,true}){
        session.swapButtons=swapped;session.toggleMenu();session.menuRow=6;
        const auto speed=session.speedIndex;
        SDL_Event pad{};pad.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        pad.gbutton.button=swapped?SDL_GAMEPAD_BUTTON_SOUTH:SDL_GAMEPAD_BUTTON_EAST;
        handleAction(session,pad);require(session.speedIndex==(speed+1)%GAME_SPEEDS.size(),"AB交換設定で機能メニューを実行できません。");
        pad.gbutton.button=swapped?SDL_GAMEPAD_BUTTON_EAST:SDL_GAMEPAD_BUTTON_SOUTH;
        handleAction(session,pad);require(!session.menuOpen,"AB交換設定で機能メニューを閉じられません。");
        session.setSpeed(speed);
    }
    session.focused=oldFocus;
    session.swapButtons=oldSwap;session.applyStateFile(stairState);
    for (int floor = 1; floor <= 2; ++floor)
    {
        session.applyStateFile(stairState);
        SDL_Event open{};
        open.type = SDL_EVENT_KEY_DOWN;
        open.key.scancode = SDL_SCANCODE_F3;
        handleAction(session, open);
        require(session.mapOpen && session.mapFloor == 1 && session.mapX == 0 && session.mapY == 0,
            "階段の確認中に移動マップが開きません。");
        session.closeMap();
        require(memoryHash(session) == stairRam, "移動をキャンセルしても階段の確認状態が維持されません。");
        session.openMap();
        if (floor == 1) { session.mapX = 0; session.mapY = 1; }
        else session.selectMapFloor(1);
        require(findCell(session.dungeonMaps[floor - 1], session.mapX, session.mapY), "階段からの移動先がありません。");
        const auto expectedX = static_cast<uint8_t>(session.mapX);
        const auto expectedY = static_cast<uint8_t>(session.mapY);
        session.executeMapMove();
        runTestFrames(session, 60);
        auto* arrived = session.gameRam();
        const auto diagnostic = std::to_string(floor) + ": " + std::to_string(arrived[0xb5]) + "," + std::to_string(arrived[0xb6]) + "," + std::to_string(arrived[0xb7]) + " text=" + std::to_string(arrived[0x23]) + " ready=" + std::to_string(session.core.can_move());
        writeFile(session.dataDirectory / L"stairs-arrived.txt", diagnostic.data(), diagnostic.size());
        session.uiScreenshot = session.dataDirectory / L"stairs-arrived.bmp";
        session.draw();
        require(!session.mapOpen && session.core.can_move() && arrived[0xb5] == expectedX &&
            arrived[0xb6] == expectedY && arrived[0xb7] == floor && arrived[0x23] == 3,
            "階段からの移動、または確認表示の片付けに失敗しました: " + diagnostic);
        require(std::count_if(session.pixels.begin(), session.pixels.end(), [](uint8_t value) { return value != 0; }) >
            static_cast<ptrdiff_t>(session.pixels.size() / 20), "階段からの移動後に画面が描画されません。");
        const auto saved = readFile(session.slotPath(1));
        require(decodeState(saved, session.romId, session.coreId, session.core.serialize_size()) ==
            decodeState(stairState, session.romId, session.coreId, session.core.serialize_size()),
            "階段の確認中のステートが保存されませんでした。");
        session.loadState(1);
        require(memoryHash(session) == stairRam && session.core.can_move(), "階段の確認状態へ復帰できません。");
    }
    session.replayInput = true;
    tap(32, 20); tap(256, 60); tap(16, 60);
    auto* ram = session.gameRam();
    require(session.core.can_move() && ram[0xb5] == 0 && ram[0xb6] == 1 && ram[0xb7] == 1, "探索開始位置が不正です。");
    const auto baseline = encodeState(session.captureState(), session.romId, session.coreId);
    const auto baseRam = memoryHash(session);
    // 遭遇要求を立てて通常の移動処理を通し、戦闘メニューを検証用に再現する。
    ram[0xee] = 1;
    tap(16, 240);
    rejectedScene();
    session.applyStateFile(baseline);
    session.replayInput = false;
    session.inputBlocked = true;

    for (int floor = 1; floor <= 8; ++floor)
    {
        session.applyStateFile(baseline);
        session.openMap();
        for (int step = 1; step < floor; ++step) session.selectMapFloor(1);
        require(session.mapFloor == floor && findCell(session.dungeonMaps[floor - 1], session.mapX, session.mapY), "階層切替で存在しないマスが選ばれました。");
        const auto& cells = session.dungeonMaps[floor - 1];
        auto target = std::find_if(cells.begin(), cells.end(), [](const auto& cell)
        {
            return cell.event == 0 && !(cell.flags & 0x80) && (cell.x != 0 || cell.y != 1);
        });
        require(target != cells.end(), "移動検証先がありません。");
        session.mapX = target->x; session.mapY = target->y;
        session.uiScreenshot = session.dataDirectory / ("map-B" + std::to_string(floor) + ".bmp");
        session.draw();
        const auto beforeMove = session.captureState();
        const auto facing = ram[0xef];
        session.executeMapMove();
        require(!session.mapOpen && ram[0xb5] == static_cast<uint8_t>(target->x) && ram[0xb6] == static_cast<uint8_t>(target->y) && ram[0xb7] == floor && ram[0xef] == facing,
            "階層・座標・向きが一致しません。");
        const auto saved = readFile(session.slotPath(1));
        require(decodeState(saved, session.romId, session.coreId, session.core.serialize_size()) == beforeMove, "移動前のステートがスロット1に保存されませんでした。");
        for (const auto& cell : cells)
        {
            // 描画の座標変換と同じ区画インデックスで、実際に読み込まれたフラグを照合する。
            bool matched = false;
            for (int block = 0; block < 16; ++block)
            {
                const int x = cell.x + 0x81 - ram[0xe000 + block * 4];
                const int y = cell.y + 0x81 - ram[0xe001 + block * 4];
                if (x < 0 || x >= 8 || y < 0 || y >= 8) continue;
                const auto index = (ram[0xe002 + block * 4] | (ram[0xe003 + block * 4] << 8)) + (7 - y) * 8 + x;
                require(ram[0xe800 + index] == cell.flags, "移動先の階層データが更新されませんでした。");
                matched = true; break;
            }
            require(matched, "マップの区画座標が一致しません。");
        }
        session.loadState(1);
        require(memoryHash(session) == baseRam, "スロット1で移動前に戻れませんでした。");
    }

    session.applyStateFile(baseline);
    session.openMap();
    session.mapX = 127; session.mapY = 127;
    bool rejected = false;
    try { session.executeMapMove(); } catch (const std::exception&) { rejected = true; }
    require(rejected && memoryHash(session) == baseRam, "存在しないマスへの移動を拒否できませんでした。");
    session.closeMap();
    session.openMap(); session.moveMapCursor(0, 1);
    const auto original = readFile(session.slotPath(1));
    auto temporary = session.slotPath(1); temporary += L".tmp";
    fs::create_directory(temporary);
    rejected = false;
    try { session.executeMapMove(); } catch (const std::exception&) { rejected = true; }
    fs::remove(temporary);
    require(rejected && memoryHash(session) == baseRam && readFile(session.slotPath(1)) == original, "保存失敗時に移動したか、既存のスロットを壊しました。");
    session.closeMap();
    // 階段などのイベントマスも移動先に指定できる。
    session.openMap(); session.mapX = session.mapY = 0;
    require(findCell(session.dungeonMaps[0], 0, 0), "階段マスがありません。");
    session.executeMapMove();
    require(ram[0xb5] == 0 && ram[0xb6] == 0 && ram[0xb7] == 1, "イベントマスへの移動が制限されました。");
    session.applyStateFile(baseline);
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_F3;
    handleAction(session, key);
    require(session.mapOpen, "F3で移動画面が開きません。");
    key.key.scancode = SDL_SCANCODE_W;
    handleAction(session, key);
    require(session.mapFloor == 2, "Wで階層が変わりません。");
    key.key.scancode = SDL_SCANCODE_ESCAPE;
    handleAction(session, key);
    require(!session.mapOpen && memoryHash(session) == baseRam, "移動画面のキャンセルでゲームが変化しました。");

    sdlCheck(SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"));
    SDL_VirtualJoystickDesc descriptor{};
    SDL_INIT_INTERFACE(&descriptor);
    descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    descriptor.name = "WiizV movement test pad";
    descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT;
    descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    const auto id = SDL_AttachVirtualJoystick(&descriptor);
    require(id != 0, SDL_GetError());
    session.openPad(id);
    require(session.gamepads.contains(id), "仮想パッドを開けません。");
    auto* joystick = SDL_GetGamepadJoystick(session.gamepads.at(id));
    sdlCheck(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768));
    auto trigger = [&](Sint16 value)
    {
        sdlCheck(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, value));
        SDL_UpdateJoysticks(); pollExtraControls(session);
    };
    trigger(-32768);
    session.focused = false;
    trigger(32767);
    session.focused = true;
    trigger(32767);
    require(!session.mapOpen, "非アクティブ中のRT押下が持ち越されました。");
    trigger(-32768); trigger(32767);
    require(session.mapOpen, "RTで移動画面が開きません。");
    SDL_Event button{};
    button.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    button.gbutton.button = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    handleAction(session, button);
    require(session.mapFloor == 2, "RBで階層が変わりません。");
    button.gbutton.button = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    handleAction(session, button);
    require(session.mapFloor == 1, "LBで階層が変わりません。");
    const auto cursorX = session.mapX;
    sdlCheck(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 32767));
    SDL_UpdateJoysticks(); pollExtraControls(session);
    require(session.mapX > cursorX, "左スティックで座標が変わりません。");
    const auto movedX = session.mapX;
    pollExtraControls(session);
    require(session.mapX == movedX, "左スティックの初回リピート待ちがありません。");
    sdlCheck(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0));
    SDL_UpdateJoysticks();
    session.closeMap(); trigger(32767);
    require(!session.mapOpen, "RT押しっぱなしで移動画面が再度開きました。");
    SDL_CloseGamepad(session.gamepads.at(id)); session.gamepads.erase(id);
    session.triggerHeld.erase(id); session.moveTriggerHeld.erase(id);
    sdlCheck(SDL_DetachVirtualJoystick(id));
    require(memoryHash(session) == baseRam, "移動先選択中にゲームが進行しました。");
#ifdef __ANDROID__
    // マップを閉じた指の移動・押し続けが、次の機能ボタンを押してはいけない。
    const bool savedFocus=session.focused;session.focused=true;
    for(int attempt=0;attempt<3;++attempt){
        session.openMap();androidDraw(session);
        const auto control=std::find_if(androidButtons.begin(),androidButtons.end(),[](const auto& button){return button.action==21;});
        require(control!=androidButtons.end(),"Androidマップの戻るパッドがありません。");
        int width=0,height=0;renderOutputSize(session.renderer,&width,&height);
        SDL_Event finger{};finger.type=SDL_EVENT_FINGER_DOWN;finger.tfinger.fingerID=987;
        finger.tfinger.x=(control->box.x+control->box.w/2)/width;finger.tfinger.y=(control->box.y+control->box.h/2)/height;
        androidEvent(session,finger);require(!session.mapOpen&&!session.menuOpen,"戻るパッドでゲームへ戻れません。");
        androidDraw(session);
        const auto menu=std::find_if(androidButtons.begin(),androidButtons.end(),[](const auto& button){return button.action==1;});
        require(menu!=androidButtons.end(),"通常画面の機能ボタンがありません。");
        finger.type=SDL_EVENT_FINGER_MOTION;finger.tfinger.x=(menu->box.x+menu->box.w/2)/width;finger.tfinger.y=(menu->box.y+menu->box.h/2)/height;
        androidEvent(session,finger);androidTick(session);
        require(!session.menuOpen&&!session.mapOpen&&androidTouchMask==0,"マップを閉じた指が機能メニューへ抜けました。");
        finger.type=SDL_EVENT_FINGER_UP;androidEvent(session,finger);
        const auto speed=std::find_if(androidButtons.begin(),androidButtons.end(),[](const auto& button){return button.action==5;});
        const int previousSpeed=session.speedIndex;
        finger.type=SDL_EVENT_FINGER_DOWN;finger.tfinger.x=(speed->box.x+speed->box.w/2)/width;finger.tfinger.y=(speed->box.y+speed->box.h/2)/height;
        androidEvent(session,finger);finger.type=SDL_EVENT_FINGER_UP;androidEvent(session,finger);
        require(session.speedIndex==(previousSpeed+1)%5,"指を離した後の新しいタップが無効になりました。");
        session.setSpeed(previousSpeed);
    }
    session.openMap();
    SDL_Event back{};back.type=SDL_EVENT_KEY_DOWN;back.key.scancode=SDL_SCANCODE_AC_BACK;androidEvent(session,back);
    back.key.repeat=true;androidEvent(session,back);
    require(!session.mapOpen&&!session.menuOpen,"戻るキーの繰返しで機能メニューが開きました。");
    back.type=SDL_EVENT_KEY_UP;androidEvent(session,back);session.focused=savedFocus;
    require(memoryHash(session)==baseRam,"Androidマップの操作確認中にゲーム状態が変化しました。");
#endif
    session.openMap();
    const auto report = std::string("PASS: stair prompt open/cancel/same-floor/cross-floor/cleanup/slot 1 restore; all eight floors; floor cache refreshed; facing retained; pre-move slot 1 and restore; title/castle/camp/combat rejection; invalid cell; write failure; event destination; F3; virtual-pad RT debounce/background, LB/RB, left stick\n");
    writeFile(session.dataDirectory / L"movement-check-report.txt", report.data(), report.size());
}
