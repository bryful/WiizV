#pragma once
inline void runItemChecks(Player& session) {
    require(session.core.can_edit_items&&!session.core.can_edit_items(),"タイトルでアイテム編集が許可されました。");
    session.setSpeed(0);session.replayInput=true;
    auto tap=[&](uint16_t buttons,int after=100){session.replayButtons=buttons;runTestFrames(session,10);session.replayButtons=0;runTestFrames(session,after);};
    tap(8,240);tap(256,590);tap(256);tap(256);tap(256);
    require(session.gameRam()[0xdb]>0&&session.core.can_edit_items(),"酒場のパーティーで開けません。");
    for(int i=0;i<5;++i)tap(256,50);
    tap(1);tap(16,40);tap(256,80);tap(256,300);tap(1);tap(32,20);tap(256,60);tap(16,60);
    require(session.core.can_move()&&session.core.can_edit_items(),"探索中に開けません。");
    require(ItemMenu::name(session.rom,10,0,false)==std::vector<uint8_t>({'P','I','K','E'})&&
        ItemMenu::name(session.rom,10,0x20,false)==std::vector<uint8_t>({'P','O','L','E',' ','A','R','M'}),"識別・未識別名が逆です。");
    auto baseline=encodeState(session.captureState(),session.romId,session.coreId);
    session.openItems();
    require(session.items.members.size()>=2,"編集検証用のパーティーが不足しています。");
    unsigned first=session.items.members[0],second=session.items.members[1];
    auto r=ItemMenu::load(session.gameRam(),first),other=ItemMenu::load(session.gameRam(),second);
    std::fill(r.begin()+0x43,r.begin()+0x54,uint8_t{0});std::fill(other.begin()+0x43,other.begin()+0x54,uint8_t{0});
    r[0xa]&=0xe3; // Fighter, keeping race and alignment.
    unsigned armor=0;
    for(unsigned id=1;id<ItemMenu::itemCount;++id)if(ItemMenu::info(session.rom,id,4)==1&&(ItemMenu::info(session.rom,id,19)&1)&&!ItemMenu::info(session.rom,id,6)&&ItemMenu::info(session.rom,id,5)==255){armor=id;break;}
    require(armor!=0,"装備検証用の鎧がありません。");
    r[0x53]=3;r[0x4b]=4;r[0x4c]=static_cast<uint8_t>(armor);r[0x4d]=4;r[0x45]=0x25;
    session.commitItems({{first,r},{second,other}});
    auto invoke=[&](unsigned action,unsigned row){session.items.focus=0;session.items.row[0]=row;session.items.submenu=true;session.items.action=action;session.items.execute(session.gameRam(),session.rom);};
    const auto ramBeforeEquipment=std::vector<uint8_t>(session.gameRam(),session.gameRam()+0x20000);
    invoke(0,1);
    auto after=ItemMenu::load(session.gameRam(),first);
    require((after[0x44]&0x80)&&static_cast<int8_t>(after[0x26])==10-static_cast<int>(ItemMenu::info(session.rom,armor,24)),"装備・原作AC再計算が一致しません。");
    for(unsigned i=0;i<0x20000;++i)if(i<0x1000+first*128||i>=0x1080+first*128)
        require(ramBeforeEquipment[i]==session.gameRam()[i],"編集中に原作の場面が進行しました。");
    session.draw(); // Present once before capturing the newly opened overlay.
    session.uiScreenshot=session.dataDirectory/L"item-equipped.bmp";session.draw();
    invoke(1,1);require(ItemMenu::load(session.gameRam(),first)[0x53]==3,"装備品を移動しました。");
    invoke(0,1);require(!(ItemMenu::load(session.gameRam(),first)[0x44]&0x80),"装備解除できません。");
    invoke(3,2);require(session.items.row[0]==1&&ItemMenu::load(session.gameRam(),first)[0x44]==0x25,"並べ替えでフラグまたは選択が失われました。");
    invoke(1,1);require(ItemMenu::load(session.gameRam(),second)[0x43]==0x25,"受け渡しで状態が失われました。");
    session.items.submenu=false;session.items.bulk(session.gameRam(),true);
    require(ItemMenu::count(ItemMenu::load(session.gameRam(),first))==3&&ItemMenu::count(ItemMenu::load(session.gameRam(),second))==0,"未識別品を収集できません。");
    invoke(2,0);require(session.items.deleting&&!session.items.yes,"削除確認が安全な初期選択ではありません。");
    session.items.execute(session.gameRam(),session.rom);require(ItemMenu::count(ItemMenu::load(session.gameRam(),first))==3,"削除取消で品が消えました。");
    invoke(2,0);session.items.yes=true;session.items.execute(session.gameRam(),session.rom);
    require(ItemMenu::count(ItemMenu::load(session.gameRam(),first))==2,"削除できません。");
    session.items.submenu=false;session.items.bulk(session.gameRam(),false);
    require(ItemMenu::count(ItemMenu::load(session.gameRam(),first))==0&&ItemMenu::count(ItemMenu::load(session.gameRam(),second))==2,"一括移動できません。");
    auto* sram=static_cast<const uint8_t*>(session.core.get_memory_data(RETRO_MEMORY_SAVE_RAM));
    for(unsigned slot:{first,second}){
        auto record=ItemMenu::load(session.gameRam(),slot),checksum=record;
        require(characterData::updateChecksum(checksum.data(),session.rom.data(),session.rom.size())&&record==checksum,"チェックサム不一致です。");
        require(std::equal(record.begin(),record.end(),sram+slot*128)&&std::equal(record.begin(),record.end(),sram+0xa00+slot*128),"二重SRAMと一致しません。");
    }
    // Failure must roll back every participant, including SRAM.
    const auto ramBefore=memoryHash(session);const auto saveBefore=readFile(session.savePath);
    auto blocked=session.savePath;blocked+=L".tmp";fs::create_directory(blocked);
    bool failed=false;
    try{session.items.focus=1;session.items.bulk(session.gameRam(),false);}catch(...){failed=true;}
    fs::remove(blocked);
    require(failed&&memoryHash(session)==ramBefore&&readFile(session.savePath)==saveBefore,"保存失敗で編集が残りました。");
    session.items.focus=0;session.uiScreenshot=session.dataDirectory/L"item-editor.bmp";session.draw();
    session.closeItems();session.applyStateFile(baseline);session.gameRam()[0xee]=1;tap(16,240);
    require(!session.core.can_edit_items(),"戦闘中にアイテム編集が許可されました。");
    session.applyStateFile(baseline);session.replayInput=false;session.inputBlocked=true;
    const std::string report="PASS: party access, native equipment AC, flags, transfer, sorting, deletion confirmation, bulk actions, SRAM/checksums, failed-save rollback, battle rejection\n";
    writeFile(session.dataDirectory/L"item-check-report.txt",report.data(),report.size());
}
