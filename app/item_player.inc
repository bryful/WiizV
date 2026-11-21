    ItemMenu items;
    SDL_FRect itemArea{};
    int itemStickX=0,itemStickY=0;
    Uint64 itemRepeatAt=0;

    void closeItems() { items.open=false;inputBlocked=true;SDL_ClearAudioStream(audio); }
    void openItems() {
        require(!stateRecoveryFailed&&core.can_edit_items&&core.request_item_recalc&&core.gold_status&&core.cancel_gold&&core.can_edit_items(),
            "１人以上のパーティーで入力待ち中に開いてください。戦闘中は使えません。");
        require(gameRam()[0xdb]<=6,"パーティー情報が不正です。");
        for(unsigned i=0;i<gameRam()[0xdb];++i)require(ItemMenu::valid(ItemMenu::load(gameRam(),gameRam()[0xdc+i])),"所持品データが不正です。");
        items.begin(gameRam());items.commit=[this](const ItemMenu::Changes& changes){commitItems(changes);};
        menuOpen=mapOpen=characterMenu=effectsMenu=false;inputBlocked=true;SDL_ClearAudioStream(audio);
    }
    void commitItems(const ItemMenu::Changes& changes) {
        require(items.open&&!stateRecoveryFailed&&core.can_edit_items&&core.can_edit_items(),"現在はアイテムを編集できません。");
        auto* sram=static_cast<uint8_t*>(core.get_memory_data(RETRO_MEMORY_SAVE_RAM));
        require(sram&&core.get_memory_size(RETRO_MEMORY_SAVE_RAM)>=0x1400,"通常セーブ領域が不正です。");
        const auto before=captureState();
        auto edited=changes;
        const bool wasReplay=replayInput;const auto oldButtons=replayButtons;
        replayInput=false;replayButtons=gameButtons=0;
        auto restore=[&] {
            core.cancel_gold();applyAudioSpeed(0);
            const bool restored=core.unserialize(before.data(),before.size());applyAudioSpeed(speedIndex);
            replayInput=wasReplay;replayButtons=oldButtons;
            if(!restored){stateRecoveryFailed=true;throw std::runtime_error("編集前の状態を復元できません。ROMを開き直してください。");}
            SDL_ClearAudioStream(audio);
        };
        try {
            unsigned seen=0;
            for(auto& change:edited) {
                require(change.first<20&&std::find(items.members.begin(),items.members.end(),change.first)!=items.members.end()&&
                    !(seen&(1u<<change.first))&&ItemMenu::valid(change.second),"所持品の更新内容が不正です。");
                seen|=1u<<change.first;
                std::copy(change.second.begin(),change.second.end(),gameRam()+0x1000+change.first*128);
                require(core.request_item_recalc(change.first),"能力再計算を開始できません。");
                for(unsigned frame=0;frame<12&&core.gold_status()!=2;++frame){core.run();require(callbackError.empty(),callbackError);}
                require(core.gold_status()==2,"能力再計算が完了しませんでした。");
                const auto* computed=gameRam()+0x1000+change.first*128;
                for(unsigned offset:{0x26u,0x59u,0x6bu,0x6cu,0x6du,0x6eu,0x6fu})change.second[offset]=computed[offset];
                require(characterData::updateChecksum(change.second.data(),rom.data(),rom.size()),"チェックサムを更新できません。");
                restore();replayInput=false;replayButtons=0;
            }
            restore();
            const auto backup=encodeState(before,romId,coreId);fs::create_directories(stateDirectory);writeFile(editBackupPath(),backup.data(),backup.size());
            for(const auto& change:edited) {
                std::copy(change.second.begin(),change.second.end(),gameRam()+0x1000+change.first*128);
                std::copy(change.second.begin(),change.second.end(),sram+change.first*128);
                std::copy(change.second.begin(),change.second.end(),sram+0xa00+change.first*128);
                // The original equipment dialog keeps a temporary inventory at $50.
                // Synchronize it as well, so leaving that dialog cannot restore old items.
                auto* ram=gameRam();unsigned work=ram[0x30]|(ram[0x31]<<8);
                unsigned stack=ram[0x1a]|(ram[0x1b]<<8);
                if(work==0x1000+change.first*128&&stack>=0x1f00&&stack<0x1fe0)
                    for(unsigned i=stack+1;i+2<0x2000;++i)
                        if(ram[i]==0x71&&ram[i+1]==0xaa&&ram[i+2]==4){std::copy_n(change.second.begin()+0x43,17,ram+0x50);break;}
            }
            saveRam();
        }catch(...){restore();throw;}
        inputBlocked=true;
    }
    void itemCommand(int command) {
        if(!items.open)return;
        if(command==0)items.move(0,-1);if(command==1)items.move(0,1);
        if(command==2)items.move(-1,0);if(command==3)items.move(1,0);
        if(command==4)items.change(-1);if(command==5)items.change(1);
        if(command==6)items.execute(gameRam(),rom);
        if(command==7){items.back();if(!items.open)closeItems();}
    }
    void itemClick(float x,float y) {
        if(x<itemArea.x||y<itemArea.y||x>=itemArea.x+itemArea.w||y>=itemArea.y+itemArea.h)return;
        x=(x-itemArea.x)*512/itemArea.w;y=(y-itemArea.y)*448/itemArea.h;
        if(items.deleting){if(y>=210&&y<252){items.yes=x>=256;itemCommand(6);}return;}
        if(items.submenu){if(x>=132&&x<380&&y>=126&&y<276){items.action=static_cast<unsigned>((y-126)/30);itemCommand(6);}return;}
        unsigned side=x<256?0:1;items.focus=side;
        if(y>=12&&y<48){if(x-side*256<40)itemCommand(4);else if(x-side*256>=216)itemCommand(5);return;}
        if(y>=60&&y<300){items.row[side]=static_cast<unsigned>((y-60)/30);itemCommand(6);}
        if(y>=312&&y<372){items.row[side]=8+static_cast<unsigned>((y-312)/30);itemCommand(6);}
        if(y>=416)itemCommand(7);
    }
    void drawItems(const SDL_FRect& area) {
        itemArea=area;
        auto box=[&](float x,float y,float w,float h){return SDL_FRect{area.x+x*area.w/512,area.y+y*area.h/448,w*area.w/512,h*area.h/448};};
        auto label=[&](float x,float y,const std::string& text,float maxWidth=480.0f){itemLabel(renderer,area.x+x*area.w/512,area.y+y*area.h/448,18*area.h/448,text,maxWidth*area.w/512);};
        auto rect=[&](float x,float y,float w,float h){auto r=box(x,y,w,h);SDL_RenderRect(renderer,&r);};
        auto glyphs=[&](float x,float y,const std::vector<uint8_t>& text,float scale=1.5f) {
            const auto* vram=static_cast<const uint8_t*>(core.get_memory_data(RETRO_MEMORY_VIDEO_RAM));
            if(!vram||core.get_memory_size(RETRO_MEMORY_VIDEO_RAM)<0x5000)return;
            for(unsigned letter=0;letter<text.size();++letter)for(unsigned yy=0;yy<8;++yy)for(unsigned xx=0;xx<8;++xx){
                auto* glyph=vram+0x4000+text[letter]*16;
                if((glyph[yy*2]&glyph[yy*2+1])&(128>>xx)){auto dot=box(x+(letter*8+xx)*scale,y+yy*scale,scale,scale);SDL_RenderFillRect(renderer,&dot);}
            }
        };
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderFillRect(renderer,&area);
        SDL_SetRenderDrawColor(renderer,235,235,225,255);rect(1,1,510,446);rect(3,3,506,408);rect(256,3,0,298);
        for(unsigned p=0;p<2;++p) {
            unsigned x=p*256;if(items.focus==p)rect(x+6,8,244,40);bool absent=p==1&&items.members.size()<2;
            label(x+10,18,"〈");label(x+224,18,"〉");
            if(absent){label(x+72,18,"相手なし");continue;}
            auto r=items.record(gameRam(),p);
            glyphs(x+48,21,std::vector<uint8_t>(r.begin()+2,r.begin()+10),2);
            for(unsigned i=0;i<8;++i) {
                if(items.focus==p&&items.row[p]==i){SDL_SetRenderDrawColor(renderer,85,85,75,255);auto select=box(x+6,56+i*30,244,28);SDL_RenderFillRect(renderer,&select);}
                SDL_SetRenderDrawColor(renderer,235,235,225,255);
                if(i<ItemMenu::count(r)) {
                    label(x+10,62+i*30,(r[0x43+i]&0x80)?"＊":" ");
                    glyphs(x+32,65+i*30,ItemMenu::name(rom,r[0x4b+i],r[0x43+i],gameRam()[0xd0]!=0));
                }
            }
        }
        for(unsigned r=8;r<10;++r){
            if(items.row[items.focus]==r){SDL_SetRenderDrawColor(renderer,85,85,75,255);auto select=box(6,308+(r-8)*30,500,28);SDL_RenderFillRect(renderer,&select);}
            SDL_SetRenderDrawColor(renderer,235,235,225,255);label(16,314+(r-8)*30,r==8?"未識別アイテムを集める":"未装備アイテムを相手へ移動");
        }
        label(12,382,items.message.empty()?"左右：選択  L/R・B/N：キャラ変更":items.message);
        label(12,422,"決定：操作  戻る：ゲームへ");
        if(items.submenu) {
            SDL_SetRenderDrawColor(renderer,0,0,0,255);auto sub=box(128,120,256,162);SDL_RenderFillRect(renderer,&sub);
            SDL_SetRenderDrawColor(renderer,235,235,225,255);rect(128,120,256,162);
            const char* actions[]{"装備","移動","削除","上に移動","下に移動"};
            auto r=items.record(gameRam(),items.focus);
            for(unsigned a=0;a<5;++a)label(140,130+a*30,std::string(items.action==a?"▶ ":"   ")+(a==0&&(r[0x43+items.row[items.focus]]&0x80)?"装備解除":actions[a]));
        }
        if(items.deleting){
            SDL_SetRenderDrawColor(renderer,0,0,0,255);auto sub=box(100,164,312,100);SDL_RenderFillRect(renderer,&sub);
            SDL_SetRenderDrawColor(renderer,235,235,225,255);rect(100,164,312,100);label(126,180,"このアイテムを削除しますか？");label(142,220,items.yes?"   いいえ       ▶ はい":"▶ いいえ          はい");
        }
    }
