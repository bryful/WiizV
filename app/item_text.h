#pragma once
#ifdef _WIN32
#pragma comment(lib, "gdi32.lib")
struct ItemTextImage { SDL_Texture* texture; int width,height; };
inline std::map<std::string,ItemTextImage> itemTextImages;
inline void clearItemText() {
    for(auto& entry:itemTextImages)SDL_DestroyTexture(entry.second.texture);
    itemTextImages.clear();
}
#endif
inline void itemLabel(SDL_Renderer* renderer,float x,float y,float height,const std::string& text,float maxWidth=10000) {
    if(text.empty())return;
#ifdef _WIN32
    auto found=itemTextImages.find(text);
    if(found==itemTextImages.end()) {
        if(itemTextImages.size()>128)clearItemText();
        int n=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
        std::wstring wide(n,L' ');MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),wide.data(),n);
        HDC dc=CreateCompatibleDC(nullptr);
        HFONT font=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH,L"ＭＳ ゴシック");
        auto oldFont=SelectObject(dc,font);SIZE size{};GetTextExtentPoint32W(dc,wide.data(),n,&size);
        int width=std::max(1,static_cast<int>(size.cx)),h=std::max(1,static_cast<int>(size.cy));
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap||!bits){SelectObject(dc,oldFont);DeleteObject(font);DeleteDC(dc);throw std::runtime_error("文字を描画できません。");}
        auto oldBitmap=SelectObject(dc,bitmap);SetBkColor(dc,RGB(0,0,0));SetTextColor(dc,RGB(255,255,255));TextOutW(dc,0,0,wide.data(),n);GdiFlush();
        auto* pixels=static_cast<uint32_t*>(bits);for(int i=0;i<width*h;++i)pixels[i]=(pixels[i]&255)?0xffffffff:0;
        SDL_Texture* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,width,h);
        if(texture){SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_UpdateTexture(texture,nullptr,bits,width*4);}
        SelectObject(dc,oldBitmap);SelectObject(dc,oldFont);DeleteObject(bitmap);DeleteObject(font);DeleteDC(dc);
        if(!texture)throw std::runtime_error(SDL_GetError());
        found=itemTextImages.emplace(text,ItemTextImage{texture,width,h}).first;
    }
    const auto& glyph=found->second;
#else
    if(androidTexts.find(text)==androidTexts.end())androidText(renderer,-10000,-10000,text);
    const auto& glyph=androidTexts.at(text);
#endif
    float width=std::min(maxWidth,height*glyph.width/glyph.height);
    SDL_FRect dest{x,y,width,height};SDL_RenderTexture(renderer,glyph.texture,nullptr,&dest);
}
