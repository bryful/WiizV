#pragma once
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

// SFC Wizardry V: eight flags, eight item IDs, then the occupied slot count.
class ItemMenu {
public:
    using Record = std::array<uint8_t,128>;
    using Changes = std::vector<std::pair<unsigned,Record>>;
    bool open = false, submenu = false, deleting = false, yes = false;
    unsigned focus = 0, side[2]{0,0}, row[2]{0,0}, action = 0;
    std::vector<unsigned> members;
    std::string message;
    std::function<void(const Changes&)> commit;
    static constexpr unsigned capacity = 8, itemCount = 136;
    static unsigned count(const Record& r) { return r[0x53]; }
    static bool valid(const Record& r) {
        if (!r[0] || count(r)>capacity) return false;
        for(unsigned i=0;i<count(r);++i) if(r[0x4b+i]>=itemCount) return false;
        return true;
    }
    static void erase(Record& r,unsigned index) {
        for(unsigned i=index;i+1<count(r);++i){r[0x43+i]=r[0x44+i];r[0x4b+i]=r[0x4c+i];}
        --r[0x53];r[0x43+count(r)]=r[0x4b+count(r)]=0;
    }
    static Record load(const uint8_t* ram,unsigned slot) {
        Record r{};std::copy_n(ram+0x1000+slot*128,128,r.begin());return r;
    }
    static unsigned info(const std::vector<uint8_t>& rom,unsigned id,unsigned field) {
        return id<itemCount && rom.size()>0x1bb71+id*38+field ? rom[0x1bb71+id*38+field] : 255;
    }
    static std::vector<uint8_t> name(const std::vector<uint8_t>& rom,unsigned id,unsigned flags,bool japanese) {
        unsigned off=(flags&0x20)?2:0;
        unsigned ptr=info(rom,id,off)|(info(rom,id,off+1)<<8);
        if(ptr<0x8000 || ptr>=0x10000 || rom.size()<0x20000)return {};
        size_t at=0x18000+(ptr&0x7fff);
        if(japanese){while(at<0x20000 && rom[at])++at;++at;}
        std::vector<uint8_t> result;
        for(unsigned i=0;i<16 && at<0x20000 && rom[at];++i,++at)result.push_back(rom[at]);
        return result;
    }
    void begin(const uint8_t* ram) {
        members.clear();for(unsigned i=0;i<ram[0xdb];++i)members.push_back(ram[0xdc+i]);
        side[0]=0;side[1]=members.size()>1?1:0;row[0]=row[1]=focus=action=0;
        submenu=deleting=yes=false;message.clear();open=true;
    }
    Record record(const uint8_t* ram,unsigned panel) const { return load(ram,members[side[panel]]); }
    void change(int direction) {
        if(submenu||members.size()<2)return;
        do{side[focus]=(side[focus]+static_cast<unsigned>(members.size())+direction)%static_cast<unsigned>(members.size());}
        while(members.size()>1&&side[focus]==side[1-focus]);
        row[focus]=0;message.clear();
    }
    void move(int dx,int dy) {
        if(deleting){if(dx||dy)yes=!yes;return;}
        if(submenu){if(dy)action=(action+5+dy)%5;return;}
        if(dx)focus=dx<0?0:1;
        if(dy)row[focus]=(row[focus]+10+dy)%10;
    }
    void back() {if(deleting)deleting=false;else if(submenu)submenu=false;else open=false;}
    void clamp(const uint8_t* ram) {
        for(unsigned p=0;p<2;++p)if(row[p]<8)row[p]=std::min(row[p],count(record(ram,p))?count(record(ram,p))-1:0u);
    }
    void execute(const uint8_t* ram,const std::vector<uint8_t>& rom) {
        if(members.size()==1&&focus==1){message="相手がいません";return;}
        if(deleting){deleting=false;if(!yes)return;}
        else if(!submenu){
            if(row[focus]>=8){bulk(ram,row[focus]==8);return;}
            if(row[focus]>=count(record(ram,focus)))return;
            submenu=true;action=0;return;
        }
        else if(action==2){deleting=true;yes=false;return;}
        Record r=record(ram,focus);unsigned i=row[focus],n=count(r),flags=r[0x43+i],id=r[0x4b+i];
        if(i>=n)return;
        unsigned slot=members[side[focus]];
        if(action==0){
            if(flags&0x40){message="呪われています";return;}
            if(flags&0x80)r[0x43+i]&=0x7f;
            else {
                unsigned type=info(rom,id,4),job=(r[0xa]>>2)&7;
                if((flags&0x20)||type>=6||!(info(rom,id,19)&(1u<<job))){message="装備できません";return;}
                for(unsigned j=0;j<n;++j)if((r[0x43+j]&0x80)&&info(rom,r[0x4b+j],4)==type){
                    if(r[0x43+j]&0x40){message="呪われた装備があります";return;}r[0x43+j]&=0x7f;
                }
                r[0x43+i]|=0x80;
                // Original 00:9F7B marks cursed and alignment-incompatible equipment.
                if(info(rom,id,6)||(!(info(rom,id,5)&0x80)&&info(rom,id,5)!=unsigned(r[0xa]&3)))r[0x43+i]=0xc0;
            }
        }else if(action==1){
            if(flags&0xc0){message="装備・呪いの品は移動できません";return;}
            if(members.size()<2){message="相手がいません";return;}
            auto other=record(ram,1-focus);unsigned dest=count(other);
            if(dest==8){message="移動先に空きがありません";return;}
            other[0x4b+dest]=static_cast<uint8_t>(id);other[0x43+dest]=static_cast<uint8_t>(flags);++other[0x53];erase(r,i);
            commit({{slot,r},{members[side[1-focus]],other}});submenu=false;clamp(ram);message="移動しました";return;
        }else if(action==2){if(flags&0xc0){message="装備・呪いの品は削除できません";return;}erase(r,i);}
        else {
            if((action==3&&i==0)||(action==4&&i+1>=n))return;
            unsigned dest=action==3?i-1:i+1;
            std::swap(r[0x43+i],r[0x43+dest]);std::swap(r[0x4b+i],r[0x4b+dest]);
            commit({{slot,r}});row[focus]=dest;submenu=false;message.clear();return;
        }
        commit({{slot,r}});submenu=false;clamp(ram);message="変更しました";
    }
    void bulk(const uint8_t* ram,bool collect) {
        if(!collect&&members.size()<2){message="相手がいません";return;}
        unsigned destIndex=side[collect?focus:1-focus];Record dest=load(ram,members[destIndex]);unsigned moved=0;
        Changes changes;
        for(unsigned member=0;member<members.size()&&count(dest)<8;++member){
            if(member==destIndex||(!collect&&member!=side[focus]))continue;
            auto src=load(ram,members[member]);bool changed=false;
            for(unsigned i=0;i<count(src)&&count(dest)<8;){
                unsigned f=src[0x43+i];
                if(!(f&0xc0)&&(!collect||(f&0x20))){unsigned n=count(dest);dest[0x43+n]=static_cast<uint8_t>(f);dest[0x4b+n]=src[0x4b+i];++dest[0x53];erase(src,i);++moved;changed=true;}else ++i;
            }
            if(changed)changes.push_back({members[member],src});
        }
        if(!moved){message="移動できる品がありません";return;}
        changes.push_back({members[destIndex],dest});commit(changes);clamp(ram);message=std::to_string(moved)+"個 移動しました";
    }
};
