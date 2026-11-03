#pragma once
#include <memory>
#include <cmath>

inline Uint32 androidCommandEvent = 0;
inline int androidMenuPage = 0;
struct AndroidButton { SDL_FRect box; std::string label; int action; uint16_t mask; bool circle = false; };
struct AndroidFinger { int action = 0; uint16_t mask = 0; Uint64 since = 0, repeat = 0; bool fired = false, blocked = false; };
inline std::vector<AndroidButton> androidButtons;
inline std::map<SDL_FingerID, AndroidFinger> androidFingers;
inline std::array<unsigned,12> androidTouchPulse{};
inline Player::MapLayout androidMapGrid{};
inline SDL_FRect androidGameArea{};
inline SDL_FRect androidMapArea{};
inline float androidMapZoom = 32, androidMapCenterX = 0, androidMapCenterY = 0;
inline int androidMapFloorSeen = 0, androidMapSelectedX = 999, androidMapSelectedY = 999;
inline bool androidMapOverview = false, androidBackHeld = false;
inline int androidMenuDirection = -1;
inline Uint64 androidMenuRepeatAt = 0;

inline void androidMenuKey(int key)
{
    AndroidCall call;call.env->CallVoidMethod(call.activity,call.method("menuKey","(I)V"),key);call.check();
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_wiizv_app_GameActivity_postCommand(JNIEnv* env, jclass, jbyteArray value)
{
    if (!androidCommandEvent) return JNI_FALSE;
    const auto size = env->GetArrayLength(value);
    if (size < 1 || size > 4096) return JNI_FALSE;
    auto command = std::make_unique<std::string>(static_cast<size_t>(size), '\0');
    env->GetByteArrayRegion(value, 0, size, reinterpret_cast<jbyte*>(command->data()));
    SDL_Event event{}; event.type = androidCommandEvent; event.user.data1 = command.get();
    if (!SDL_PushEvent(&event)) return JNI_FALSE;
    command.release(); return JNI_TRUE;
}

inline void androidMenu(Player& session)
{
    session.refreshSlots();
    unsigned slots = 0;
    for (unsigned i = 0; i < 5; ++i) if (session.occupiedSlots[i]) slots |= 1u << i;
    std::ostringstream text;
    text << androidMenuPage << '|' << session.speedIndex << '|' << session.swapButtons << '|' << session.maximumBonus << '|'
        << session.skipMakerLogo << '|' << session.mixedAlignment << '|' << session.crtDisplay << '|' << session.effectMask << '|'
        << slots << '|' << (session.core.can_edit_character && session.core.can_edit_character()) << '|' << androidMenuSerial << '\n';
    const auto* vram = static_cast<const uint8_t*>(session.core.get_memory_data(RETRO_MEMORY_VIDEO_RAM));
    for (unsigned character : session.registeredCharacters())
    {
        const auto* record = session.gameRam() + 0x1000 + character * 0x80;
        text << character << '|';
        constexpr char hex[] = "0123456789abcdef";
        for (int letter = 0; letter < 8; ++letter)
            for (int row = 0; row < 8; ++row)
            {
                unsigned bits = 0;
                if (vram && session.core.get_memory_size(RETRO_MEMORY_VIDEO_RAM) >= 0x5000)
                {
                    const auto* glyph = vram + 0x4000 + record[2 + letter] * 16;
                    bits = glyph[row * 2] & glyph[row * 2 + 1];
                }
                text << hex[bits >> 4] << hex[bits & 15];
            }
        uint64_t gold = 0, experience = 0;
        const bool validGold = characterData::readGold(record, gold), validExperience = characterData::readExperience(record, experience);
        text << '|' << (validGold ? std::to_string(gold) : "-1") << '|' << (validExperience ? std::to_string(experience) : "-1")
            << '|' << (record[0xa] & 3) << '|' << characterData::level(record);
        for (unsigned levels = 1; levels <= 3; ++levels)
        {
            uint64_t target = 0;
            const bool valid = characterData::experienceTarget(record, session.rom.data(), session.rom.size(), levels, target);
            text << '|' << (valid ? std::to_string(target) : "-1");
        }
        text << '|' << characterData::allSpellsLearned(record) << '\n';
    }
    androidShowMenu(text.str());
}

inline void androidSuspend(Player& session)
{
    session.saveRam();
    if (session.stateRecoveryFailed || !session.core.loaded) return;
    const auto file = encodeState(session.captureState(), session.romId, session.coreId);
    writeFile(session.stateDirectory / (session.romPath.stem().string() + ".resume.wvs"), file.data(), file.size());
}

inline void androidReset(Player& session)
{
    session.saveRam();session.core.reset();session.menuOpen=false;session.mapOpen=false;
    session.characterMenu=false;session.paused=false;session.inputBlocked=true;
    SDL_ClearAudioStream(session.audio);androidSuspend(session);
}

inline void androidAction(Player& session, int action)
{
    if (action == 1) { androidMenuPage = 0; session.toggleMenu(); }
    else if (action == 2) session.saveState(1);
    else if (action == 3) { if (session.mapOpen) session.closeMap(); else { session.openMap(); androidMapFloorSeen = 0; } }
    else if (action == 4) session.loadState(1);
    else if (action == 5) session.setSpeed((session.speedIndex + 1) % 5);
    else if (action == 6) androidChooseRom();
    else if (action == 7) androidReset(session);
    else if (session.mapOpen)
    {
        if (action == 30 || action == 31) { androidMapOverview = false; androidMapZoom = std::clamp(androidMapZoom * (action == 30 ? 0.8f : 1.25f), 16.0f, 64.0f); }
        if (action == 32) { androidMapOverview = !androidMapOverview; androidMapCenterX = static_cast<float>(session.mapX); androidMapCenterY = static_cast<float>(session.mapY); }
        if (action == 33) { androidMapOverview = false; androidMapCenterX = static_cast<float>(session.mapX); androidMapCenterY = static_cast<float>(session.mapY); }
        if (action == 34) {
            // 離れた地獄などの区画にも、長いドラッグなしで切り替える。
            const auto& cells=session.dungeonMaps[session.mapFloor-1];
            std::vector<std::vector<size_t>> regions;
            std::vector<bool> visited(cells.size());
            size_t current=0;
            for(size_t start=0;start<cells.size();++start)if(!visited[start]){
                std::vector<size_t> region{start};visited[start]=true;
                for(size_t next=0;next<region.size();++next){const auto& from=cells[region[next]];
                    for(size_t i=0;i<cells.size();++i)if(!visited[i]&&std::abs(cells[i].x-from.x)+std::abs(cells[i].y-from.y)==1){visited[i]=true;region.push_back(i);}
                }
                if(std::any_of(region.begin(),region.end(),[&](size_t i){return cells[i].x==session.mapX&&cells[i].y==session.mapY;}))current=regions.size();
                regions.push_back(std::move(region));
            }
            const auto& target=cells[regions[(current+1)%regions.size()].front()];
            session.mapX=target.x;session.mapY=target.y;androidMapOverview=false;
            androidMapCenterX=static_cast<float>(target.x);androidMapCenterY=static_cast<float>(target.y);
        }
        if (action == 14) session.moveMapCursor(0, 1);
        if (action == 15) session.moveMapCursor(0, -1);
        if (action == 16) session.moveMapCursor(-1, 0);
        if (action == 17) session.moveMapCursor(1, 0);
        if (action == 18) session.selectMapFloor(-1);
        if (action == 19) session.selectMapFloor(1);
        if (action == 20) session.executeMapMove();
        if (action == 21) session.closeMap();
    }
}

inline bool androidContains(const SDL_FRect& rectangle, float x, float y)
{
    return x >= rectangle.x && x < rectangle.x + rectangle.w && y >= rectangle.y && y < rectangle.y + rectangle.h;
}

inline bool androidEvent(Player& session, const SDL_Event& event)
{
    // Androidのタッチ操作をWindows用マウスUIへ重ねて渡さない。
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||event.type==SDL_EVENT_MOUSE_BUTTON_UP)return true;
    if(session.menuOpen&&event.type==SDL_EVENT_KEY_DOWN){
        int key=-1;
        switch(event.key.scancode){
            case SDL_SCANCODE_UP:key=2;break;case SDL_SCANCODE_DOWN:key=3;break;
            case SDL_SCANCODE_LEFT:key=4;break;case SDL_SCANCODE_RIGHT:key=5;break;
            case SDL_SCANCODE_RETURN:case SDL_SCANCODE_KP_ENTER:key=0;break;
            case SDL_SCANCODE_ESCAPE:case SDL_SCANCODE_AC_BACK:key=1;break;
            case SDL_SCANCODE_X:key=session.swapButtons?1:0;break;
            case SDL_SCANCODE_Z:key=session.swapButtons?0:1;break;
            default:break;
        }
        if(key>=0&&(!event.key.repeat||key>=2))androidMenuKey(key);
        return true;
    }
    if(session.mapOpen&&event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN){
        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_NORTH){androidAction(session,session.swapButtons?30:31);return true;}
        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_WEST){androidAction(session,session.swapButtons?31:30);return true;}
        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_BACK){androidAction(session,32);return true;}
    }
    if(session.menuOpen&&event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        int key=-1;
        switch(event.gbutton.button){
            case SDL_GAMEPAD_BUTTON_EAST:key=session.swapButtons?1:0;break;case SDL_GAMEPAD_BUTTON_SOUTH:key=session.swapButtons?0:1;break;
            case SDL_GAMEPAD_BUTTON_DPAD_UP:key=2;break;case SDL_GAMEPAD_BUTTON_DPAD_DOWN:key=3;break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:key=4;break;case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:key=5;break;
            default:break;
        }
        if(key>=0){androidMenuKey(key);if(key>=2){androidMenuDirection=key;androidMenuRepeatAt=SDL_GetTicks()+300;}}
        return true;
    }
    if (event.type == androidCommandEvent)
    {
        std::unique_ptr<std::string> command(static_cast<std::string*>(event.user.data1));
        std::istringstream input(*command); std::string action; input >> action;
        if (action == "close")
        {
            session.menuOpen = false; session.characterMenu = false; androidMenuPage = 0;
            session.inputBlocked = true; return true;
        }
        if(action=="rom") { session.saveRam();loadRom(session,fs::path(command->substr(4)));return true; }
        int first = 0, second = 0; input >> first >> second;
        if (action == "save") { require(first >= 1 && first <= 5, "スロットが不正です。"); session.saveState(first); }
        else if (action == "load") { require(first >= 1 && first <= 5, "スロットが不正です。"); session.loadState(first); return true; }
        else if (action == "speed") { require(first >= 0 && first < 5, "倍率が不正です。"); session.setSpeed(first); }
        else if (action == "swap") session.toggleButtons();
        else if (action == "bonus") session.toggleMaximumBonus();
        else if (action == "logo") session.toggleSkipMakerLogo();
        else if (action == "mixed") session.toggleMixedAlignment();
        else if (action == "crt") session.toggleCrtDisplay();
        else if (action == "effect") { require(first >= 0 && first < 5, "効果が不正です。"); session.toggleEffect(static_cast<unsigned>(first)); }
        else if (action == "character") { session.openCharacterMenu(); androidMenuPage = 1; }
        else if (action == "root") { session.characterMenu = false; androidMenuPage = 0; }
        else if (action == "reset") { androidReset(session);return true; }
        else if (action == "gold" || action == "exp" || action == "alignment" || action == "undo" || action == "spells")
        {
            require(first >= 0 && first < 20, "対象キャラクターが不正です。"); session.editCharacter = static_cast<unsigned>(first);
            if (action == "gold") { require(second >= 0 && second < 13, "加算額が不正です。"); session.goldStep = static_cast<unsigned>(second); session.executeGoldAdd(); }
            if (action == "exp") { require(second >= 1 && second <= 3, "加算段数が不正です。"); session.experienceLevels = static_cast<unsigned>(second); session.executeExperienceAdd(); }
            if (action == "alignment") { session.editAlignment = static_cast<unsigned>(second); session.executeAlignmentChange(); }
            if (action == "spells") session.executeLearnAllSpells();
            if (action == "undo") session.restoreEditBackup();
        }
        else throw std::runtime_error("未対応の操作です。");
        if (session.menuOpen) androidMenu(session);
        return true;
    }
    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WILL_ENTER_BACKGROUND)
    {
        androidFingers.clear(); androidTouchMask = 0; androidTouchesHeld = false; androidTouchPulse = {};
        androidBackHeld = false;
    }
    if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && event.key.scancode == SDL_SCANCODE_AC_BACK)
    {
        if(event.type==SDL_EVENT_KEY_UP){androidBackHeld=false;return true;}
        if(androidBackHeld||event.key.repeat)return true;
        androidBackHeld=true;
        if (session.mapOpen) session.closeMap(); else { androidMenuPage = 0; session.toggleMenu(); }
        return true;
    }
    if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
        event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED) return false;
    int width = 0, height = 0; renderOutputSize(session.renderer, &width, &height);
    const float x = event.tfinger.x * width, y = event.tfinger.y * height;
    if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)
    {
        androidFingers.erase(event.tfinger.fingerID); return true;
    }
    if (!session.focused || session.menuOpen) return true;
    auto& finger = androidFingers[event.tfinger.fingerID];
    if(finger.blocked)return true;
    if(event.type==SDL_EVENT_FINGER_MOTION&&(finger.action<14||finger.action>17))return true;
    const auto hit = std::find_if(androidButtons.begin(), androidButtons.end(), [&](const auto& button) { return androidContains(button.box, x, y); });
    if (hit == androidButtons.end()) { finger.action = 0; finger.mask = 0; return true; }
    const bool fresh = event.type == SDL_EVENT_FINGER_DOWN || finger.action != hit->action;
    if (fresh)
    {
        const bool wasMap = session.mapOpen;
        const auto now = SDL_GetTicks(); finger = {hit->action, hit->mask, now, now + 300, false};
        androidAction(session, hit->action); finger.fired = true;
        // 画面が切り替わった指は、離されるまで次の画面のボタンを押せない。
        if(wasMap!=session.mapOpen){
            for(auto& [id,held]:androidFingers){(void)id;held.blocked=true;held.mask=0;}
            androidTouchPulse={};return true;
        }
        if(!wasMap && !session.menuOpen)for(unsigned id=0;id<12;++id)if(hit->mask&(1u<<id))androidTouchPulse[id]=2;
    }
    return true;
}

inline void androidTick(Player& session)
{
    androidTouchMask = 0; androidTouchesHeld = !androidFingers.empty();
    const auto now = SDL_GetTicks();
    if(session.menuOpen){
        int direction=-1;
        for(const auto& [id,pad]:session.gamepads){(void)id;
            const auto horizontal=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX),vertical=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY);
            if(vertical<-16000||SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_UP))direction=2;
            else if(vertical>16000||SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_DOWN))direction=3;
            else if(horizontal<-16000||SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_LEFT))direction=4;
            else if(horizontal>16000||SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT))direction=5;
        }
        if(direction>=0&&(direction!=androidMenuDirection||now>=androidMenuRepeatAt)){
            androidMenuKey(direction);androidMenuRepeatAt=now+(direction!=androidMenuDirection?300:100);
        }
        androidMenuDirection=direction;
    }else androidMenuDirection=-1;
    if(session.focused&&!session.menuOpen&&!session.mapOpen&&!session.paused)
        for(unsigned id=0;id<12;++id)if(androidTouchPulse[id]){androidTouchMask|=static_cast<uint16_t>(1u<<id);--androidTouchPulse[id];}
    for (auto& [id, finger] : androidFingers)
    {
        (void)id;
        if(finger.blocked)continue;
        if (session.mapOpen && finger.action >= 14 && finger.action <= 17 && now >= finger.repeat)
        {
            androidAction(session, finger.action); finger.repeat = now + 100;
        }
        if (!session.menuOpen && !session.mapOpen) androidTouchMask |= finger.mask;
    }
}

inline void androidDraw(Player& session)
{
    auto* renderer = session.renderer;
    androidUiDensity = androidDensity();
    int physicalWidth = 0, physicalHeight = 0; SDL_GetRenderOutputSize(renderer, &physicalWidth, &physicalHeight);
    const int logicalWidth = static_cast<int>(physicalWidth / androidUiDensity), logicalHeight = static_cast<int>(physicalHeight / androidUiDensity);
    SDL_SetRenderLogicalPresentation(renderer, logicalWidth, logicalHeight, SDL_LOGICAL_PRESENTATION_STRETCH);
    SDL_Rect safe{}; SDL_GetWindowSafeArea(session.window, &safe);
    // 横持ちの中央映像には、非表示のシステムバー用余白を残さない。
    if(physicalWidth>physicalHeight){safe.y=0;safe.h=physicalHeight;}
    const float x = safe.x / androidUiDensity + 8, y = safe.y / androidUiDensity + 8;
    const float width = safe.w / androidUiDensity - 16, height = safe.h / androidUiDensity - 16;
    const bool portrait = logicalHeight > logicalWidth;
    AndroidCall padCall;
    const bool touch = !padCall.env->CallBooleanMethod(padCall.activity,padCall.method("hasPhysicalGamepad","()Z"));
    padCall.check();
    float key = portrait ? std::min(64.0f,width/5.6f) : std::min(68.0f,std::max(44.0f,(width-height*session.aspect)/5.6f));
    if(portrait)key=std::min(key,std::max(32.0f,(height-302)/2.8f));
    const float pitch=key*.9f, groupSize=key+2*pitch;
    const float groupY = portrait ? y+height-groupSize-56 : y+(height-groupSize)/2;
    const float shoulderY = portrait ? groupY-52 : std::min(groupY+groupSize+8,y+height-96);
    const float auxiliaryY = shoulderY-52;
    androidButtons.clear();
    auto button = [&](SDL_FRect box, std::string label, int action, uint16_t mask = 0, bool circle = false)
    {
        androidButtons.push_back({box, std::move(label), action, mask, circle});
    };
    SDL_FRect available = portrait ? SDL_FRect{x,y+50,width,height-50} : SDL_FRect{x,y,width,height};
    if (touch && portrait) available.h = std::max(40.0f,std::min(width/session.aspect,auxiliaryY-12-available.y));
    const float gameWidth = std::min(available.w, available.h * session.aspect);
    androidGameArea = {available.x+(available.w-gameWidth)/2, available.y+(available.h-gameWidth/session.aspect)/2,gameWidth,gameWidth/session.aspect};
    const float rightSpace=x+width-(androidGameArea.x+androidGameArea.w);
    const float compactWidth=std::min(68.0f,std::max(24.0f,(rightSpace-10)/2));
    const float speedWidth=portrait?88:compactWidth, resetWidth=portrait?68:compactWidth;
    const float leftColumn=std::max(1.0f,(androidGameArea.x-x-10)/2);
    const float functionWidth=portrait?60:std::min(60.0f,leftColumn);
    const float romWidth=portrait?64:std::min(64.0f,leftColumn);
    const float romX=x+functionWidth+(portrait?8:6);
    button({x,y,functionWidth,40}, "機能", 1);
    button({romX,y,romWidth,40}, "ROM", 6);
    button({x+width-speedWidth-6-resetWidth,y,resetWidth,portrait?40.0f:36.0f}, "リセット", 7);
    button({x+width-speedWidth,y,speedWidth,portrait?40.0f:36.0f}, std::string("倍速 ") + SPEED_VALUES[session.speedIndex], 5);
    if (touch)
    {
        const float groupWidth = groupSize;
        const float left = x, right = x+width-groupWidth;
        if (portrait)
        {
            const float auxiliaryWidth = (width-18)/4;
            button({x,auxiliaryY-56,auxiliaryWidth,44}, "保存1",2);
            button({x+auxiliaryWidth+6,auxiliaryY,auxiliaryWidth,44}, session.mapOpen ? "戻る" : "移動",3);
            button({x+2*(auxiliaryWidth+6),auxiliaryY,auxiliaryWidth,44}, "機能",1);
            button({x+3*(auxiliaryWidth+6),auxiliaryY-56,auxiliaryWidth,44}, "読込1",4);
            button({left,shoulderY,76,44}, "L",18,1u<<10);
            button({right+groupWidth-76,shoulderY,76,44}, "R",19,1u<<11);
        }
        else
        {
            const float half=groupWidth/2-4;
            button({left,shoulderY,half,44}, "L",18,1u<<10);
            button({x,y+50,functionWidth,44}, "保存1",2);
            button({romX,y+50,romWidth,44}, "移動",3);
            button({right+half+8,shoulderY,half,44}, "R",19,1u<<11);
            button({x+width-speedWidth,y+44,speedWidth,40}, "読込1",4);
        }
        button({left+pitch,groupY,key,key}, "↑",14,1u<<4);
        button({left,groupY+pitch,key,key}, "←",16,1u<<6);
        button({left+2*pitch,groupY+pitch,key,key}, "→",17,1u<<7);
        button({left+pitch,groupY+2*pitch,key,key}, "↓",15,1u<<5);
        button({right+pitch,groupY,key,key},session.swapButtons?"Y":"X",22,1u<<9,true);
        button({right,groupY+pitch,key,key},session.swapButtons?"X":"Y",23,1u<<1,true);
        button({right+2*pitch,groupY+pitch,key,key},session.swapButtons?"B":"A",20,1u<<8,true);
        button({right+pitch,groupY+2*pitch,key,key},session.swapButtons?"A":"B",21,1u<<0,true);
        const float bottom = y+height-44;
        button({portrait?x+width/2-100:left,bottom,92,44}, "SELECT",24,1u<<2);
        button({portrait?x+width/2+8:right+groupWidth-92,bottom,92,44}, "START",25,1u<<3);
    }
    SDL_SetRenderDrawColor(renderer,8,10,10,255); SDL_RenderClear(renderer);
    if (session.texture && !session.mapOpen)
    {
        SDL_SetTextureScaleMode(session.texture,session.crtDisplay?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(renderer,session.texture,nullptr,&androidGameArea);
        if(session.crtDisplay) session.crtFilter.draw(renderer,androidGameArea,session.height);
    }
    if (session.mapOpen)
    {
        // 移動先の選択は十字キー、階層はL/R、決定・取消は右・下ボタン。
        androidButtons.clear();
        const bool hell=session.mapFloor==8&&(std::abs(session.mapX)>64||std::abs(session.mapY)>64);
        const auto coordinate=[](int value,const char* positive,const char* negative){return std::string(value<0?negative:positive)+std::to_string(std::abs(value));};
        androidText(renderer,x,y+10,(hell?std::string("B777"):"B"+std::to_string(session.mapFloor))+" "+coordinate(session.mapX,"E","W")+" "+coordinate(session.mapY,"N","S"));
        button({x+width-122,y,58,40},"全体",32);
        button({x+width-58,y,58,40},"区画",34);
        float mapLeft=x, mapWidth=width, mapBottom=y+height-28;
        if(touch){
            const float left=x,right=x+width-groupSize;
            button({left+pitch,groupY,key,key},"↑",14);
            button({left,groupY+pitch,key,key},"←",16);
            button({left+2*pitch,groupY+pitch,key,key},"→",17);
            button({left+pitch,groupY+2*pitch,key,key},"↓",15);
            button({right+pitch,groupY,key,key},session.swapButtons?"−":"＋",session.swapButtons?30:31,0,true);
            button({right,groupY+pitch,key,key},session.swapButtons?"＋":"−",session.swapButtons?31:30,0,true);
            button({right+2*pitch,groupY+pitch,key,key},session.swapButtons?"戻る":"移動",session.swapButtons?21:20,0,true);
            button({right+pitch,groupY+2*pitch,key,key},session.swapButtons?"移動":"戻る",session.swapButtons?20:21,0,true);
            button({left,shoulderY,76,44},"L",18);
            button({x+width-76,shoulderY,76,44},"R",19);
            if(portrait)mapBottom=shoulderY-12;
            else{mapLeft+=groupSize+12;mapWidth-=2*(groupSize+12);}
        }
        androidMapArea={mapLeft+36,y+70,mapWidth-36,std::max(32.0f,mapBottom-y-70)};
        androidText(renderer,x,y+height-22,session.swapButtons?"十字：座標  L/R：階層  下：移動  右：戻る":"十字：座標  L/R：階層  右：移動  下：戻る");
        const auto& cells = session.dungeonMaps[session.mapFloor-1];
        int minX=127,maxX=-128,minY=127,maxY=-128;
        for(const auto& cell:cells){minX=std::min(minX,cell.x);maxX=std::max(maxX,cell.x);minY=std::min(minY,cell.y);maxY=std::max(maxY,cell.y);}
        if(androidMapFloorSeen!=session.mapFloor){androidMapFloorSeen=session.mapFloor;androidMapOverview=false;androidMapCenterX=static_cast<float>(session.mapX);androidMapCenterY=static_cast<float>(session.mapY);}
        else if(androidMapSelectedX!=session.mapX||androidMapSelectedY!=session.mapY){androidMapCenterX=static_cast<float>(session.mapX);androidMapCenterY=static_cast<float>(session.mapY);}
        androidMapSelectedX=session.mapX;androidMapSelectedY=session.mapY;
        const float size=androidMapOverview?std::min((androidMapArea.w-32)/(maxX-minX+1),(androidMapArea.h-32)/(maxY-minY+1)):androidMapZoom;
        const float centerX=androidMapOverview?(minX+maxX)/2.0f:androidMapCenterX, centerY=androidMapOverview?(minY+maxY)/2.0f:androidMapCenterY;
        androidMapGrid={androidMapArea.x+androidMapArea.w/2+(minX-centerX-0.5f)*size,androidMapArea.y+androidMapArea.h/2+(centerY-maxY-0.5f)*size,size,minX,maxY};
        SDL_Rect clip{static_cast<int>(androidMapArea.x),static_cast<int>(androidMapArea.y),static_cast<int>(androidMapArea.w),static_cast<int>(androidMapArea.h)};
        SDL_SetRenderClipRect(renderer,&clip);
        const auto* ram=session.gameRam();
        for(const auto& cell:cells)
        {
            SDL_FRect box{androidMapGrid.x+(cell.x-minX)*size,androidMapGrid.y+(maxY-cell.y)*size,size,size};
            if(box.x+size<androidMapArea.x||box.x>androidMapArea.x+androidMapArea.w||box.y+size<androidMapArea.y||box.y>androidMapArea.y+androidMapArea.h)continue;
            SDL_SetRenderDrawColor(renderer,32,37,32,255);SDL_RenderFillRect(renderer,&box);
            SDL_SetRenderDrawColor(renderer,65,73,65,255);SDL_RenderRect(renderer,&box);
            auto wall=[&](unsigned type,float ax,float ay,float bx,float by){
                if(!type)return;
                SDL_SetRenderDrawColor(renderer,225,230,215,255);SDL_RenderLine(renderer,ax,ay,bx,by);
                if(type>=2&&size>=12){SDL_SetRenderDrawColor(renderer,245,185,80,255);SDL_FRect door{(ax+bx)/2-3,(ay+by)/2-3,6,6};SDL_RenderFillRect(renderer,&door);}
            };
            wall(cell.walls&3,box.x,box.y,box.x+size,box.y);
            wall((cell.walls>>2)&3,box.x+size,box.y,box.x+size,box.y+size);
            wall((cell.walls>>4)&3,box.x,box.y+size,box.x+size,box.y+size);
            wall((cell.walls>>6)&3,box.x,box.y,box.x,box.y+size);
            if(ram[0xb7]==session.mapFloor&&static_cast<int8_t>(ram[0xb5])==cell.x&&static_cast<int8_t>(ram[0xb6])==cell.y){SDL_SetRenderDrawColor(renderer,80,210,255,255);SDL_FRect marker{box.x+size*.3f,box.y+size*.3f,size*.4f,size*.4f};SDL_RenderFillRect(renderer,&marker);}
            if(cell.x==session.mapX&&cell.y==session.mapY){SDL_SetRenderDrawColor(renderer,255,215,40,255);SDL_RenderRect(renderer,&box);SDL_RenderLine(renderer,box.x,box.y,box.x+size,box.y+size);SDL_RenderLine(renderer,box.x+size,box.y,box.x,box.y+size);}
        }
        SDL_SetRenderClipRect(renderer,nullptr);
        if(size>=16){
            const int stride=size<40?2:1;
            for(int column=static_cast<int>(std::floor((androidMapArea.x-androidMapGrid.x)/size));androidMapGrid.x+column*size<androidMapArea.x+androidMapArea.w;++column){
                const float px=androidMapGrid.x+(column+.5f)*size;const int value=minX+column;
                if(value%stride==0&&px>=androidMapArea.x+8&&px<androidMapArea.x+androidMapArea.w-8)androidText(renderer,px,androidMapArea.y-20,std::to_string(value),true);
            }
            for(int row=static_cast<int>(std::floor((androidMapArea.y-androidMapGrid.y)/size));androidMapGrid.y+row*size<androidMapArea.y+androidMapArea.h;++row){
                const float py=androidMapGrid.y+(row+.5f)*size;const int value=maxY-row;
                if(value%stride==0&&py>=androidMapArea.y&&py<androidMapArea.y+androidMapArea.h-16)androidText(renderer,mapLeft,py-8,std::to_string(value));
            }
        }
    }
    for(const auto& control:androidButtons)
    {
        const bool pressed=std::any_of(androidFingers.begin(),androidFingers.end(),[&](const auto& pair){return pair.second.action==control.action;});
        SDL_SetRenderDrawColor(renderer,pressed?65:30,pressed?76:36,pressed?65:32,255);
        if(control.circle)
        {
            const float radius=control.box.w/2-2,cx=control.box.x+control.box.w/2,cy=control.box.y+control.box.h/2;
            for(int line=-static_cast<int>(radius);line<=static_cast<int>(radius);++line)
            {const float half=std::sqrt(std::max(0.0f,radius*radius-line*line));SDL_RenderLine(renderer,cx-half,cy+line,cx+half,cy+line);}
        }
        else SDL_RenderFillRect(renderer,&control.box);
        SDL_SetRenderDrawColor(renderer,170,185,163,255);if(!control.circle)SDL_RenderRect(renderer,&control.box);
        androidText(renderer,control.box.x+control.box.w/2,control.box.y+(control.box.h-20)/2,control.label,true,control.box.w-8);
    }
    SDL_RenderPresent(renderer);
}
