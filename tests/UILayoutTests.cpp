#include "../src/ui/Layout.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
    media::Layout layout; media::Config config;
    auto update = [&](bool expanded, bool settings, int page = 0) {
        layout.Update(0,0,config,expanded,settings,page,{0,0},{0,0,1920,1080});
    };
    update(false,false);
    assert(layout.width()==280 && layout.height()==70);
    config.compactLyrics=true; update(false,false);
    assert(layout.height()==102);
    config.compactLyrics=false; update(true,false);
    assert(layout.width()==340 && layout.height()==210);
    config.sourceLyrics=true; update(true,false); assert(layout.height()==360);
    config.sourceLyrics=false; config.asr=true; update(true,false); assert(layout.height()==360);
    config.asr=false;
    for (int page=0;page<=6;++page) {
        update(false,true,page);
        assert(layout.width()==340 && layout.height()==392);
        for (int row=0;row<10;++row) {
            auto bounds=layout.settingRow(row);
            assert(bounds.left==16 && bounds.right==324 && bounds.top==48+30*row);
            assert(bounds.bottom<=layout.height());
        }
    }
    assert(layout.settingsParentPage(1)==0 && layout.settingsParentPage(2)==1);
    assert(layout.settingsParentPage(3)==1 && layout.settingsParentPage(4)==0);
    assert(layout.settingsParentPage(5)==3 && layout.settingsParentPage(6)==3);
    update(true,false);
    assert(layout.button(0).left==88 && layout.button(1).left==148 && layout.button(2).left==208);
    assert(layout.seekRegion().top==126 && layout.volumeRegion().top==162);
    for (int mask=0; mask<8; ++mask) {
        config.compactPrevious=(mask&1)!=0; config.pause=(mask&2)!=0; config.compactNext=(mask&4)!=0;
        layout.Update(0,0,config,false,false,0,{0,0},{0,0,1920,1080});
        LONG last=-1;
        for(int i=0;i<3;++i) {
            auto r=layout.compactButton(i);
            if(layout.compactButtonVisible(i,config)) {
                assert(r.left>last && r.right>r.left); last=r.right;
                assert(layout.Hit(r,{r.left,r.top})); assert(!layout.Hit(r,{r.right,r.bottom}));
            } else assert(r.left==r.right);
        }
    }
    assert(layout.VolumeLevel(-1)==0 && layout.VolumeLevel(1000)==1);
    auto r=media::Layout::PopupRect({1900,1000},340,210,{0,0,1920,1080});
    assert(r.right==1920 && r.bottom==1080);
    HoverState hover;
    assert(!hover.Collapse(false,false,0,500));
    assert(!hover.Collapse(false,false,499,500));
    assert(hover.Collapse(false,false,500,500));
    assert(!hover.Collapse(true,false,501,500));
    assert(!hover.Collapse(false,true,1000,500));
    std::cout << "layout/hover tests passed\n";
}