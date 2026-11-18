#include "SettingsUI.h"
#include <commdlg.h>
#include <algorithm>

namespace media {
static RECT SettingRow(int i) { return {16,48+i*30,324,78+i*30}; }
static bool Hit(RECT r, POINT p) { return PtInRect(&r,p)!=FALSE; }
void SettingsUI::ChooseThemeColor(HWND h, int& color) {
    static COLORREF custom[16]{};
    CHOOSECOLORW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=h;
    dialog.rgbResult=static_cast<COLORREF>(color); dialog.lpCustColors=custom;
    dialog.Flags=CC_FULLOPEN|CC_RGBINIT;
    if(ChooseColorW(&dialog)) { color=static_cast<int>(dialog.rgbResult); config_.theme=5; }
}
static std::wstring ColorHex(int color) {
    wchar_t text[8]{};
    swprintf_s(text,L"#%02X%02X%02X",unsigned(GetRValue(color)),unsigned(GetGValue(color)),unsigned(GetBValue(color)));
    return text;
}
static void Box(HDC dc, RECT r, COLORREF color, int radius = 0) {
    HBRUSH brush = CreateSolidBrush(color);
    auto oldBrush = SelectObject(dc, brush); auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(brush);
}
static void Text(HDC dc, std::wstring text, RECT r, int size, COLORREF color, bool bold = false, UINT align = DT_LEFT) {
    auto font = CreateFontW(-size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto old = SelectObject(dc, font); SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | align);
    SelectObject(dc, old); DeleteObject(font);
}
void SettingsUI::SettingsHeader(HDC dc, const std::wstring& title) {
    Text(dc,title,{16,8,340-64,40},19,TextColor(config_),true);
}
void SettingsUI::SettingsCard(HDC dc, int index, const std::wstring& label, bool accent, int textRight) {
    auto r=SettingRow(index);
    Box(dc,{r.left,r.top+2,r.right,r.bottom-2},appearance_detail::Mix(BackgroundColor(config_),TextColor(config_),6),10);
    Text(dc,label,{r.left+10,r.top,textRight,r.bottom},14,accent?AccentColor(config_):TextColor(config_));
}
void SettingsUI::SettingsToggle(HDC dc, int index, const std::wstring& label, bool value) {
    SettingsCard(dc,index,label,false,276);
    auto r=SettingRow(index);
    Box(dc,{282,r.top+7,314,r.bottom-7},value?AccentColor(config_):appearance_detail::Mix(BackgroundColor(config_),MutedColor(config_),40),16);
    int x=value?300:285;
    Box(dc,{x,r.top+10,x+11,r.bottom-10},TextColor(config_),10);
}
static void Paragraph(HDC dc, const std::wstring& text, RECT r, COLORREF color) {
    auto font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto old=SelectObject(dc,font); SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,text.c_str(),-1,&r,DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,old); DeleteObject(font);
}

void SettingsUI::Render(HWND, HDC dc, int page) {
    const auto muted = MutedColor(config_);
    if (page == 5) {
        SettingsHeader(dc, L"Узор стекла");
        static const wchar_t* patternNames[] = {
            L"Soft Frost", L"Flowing Glass", L"Crystal", L"Liquid Waves",
            L"Fine Noise", L"Minimal Glass", L"Random Pattern", L"Legacy"
        };
        SettingsCard(dc, 0, L"Узор: " + std::wstring(patternNames[std::clamp(config_.glassPattern, 0, 7)]), false, 276);
        auto r0 = SettingRow(0);
        Box(dc, {282, r0.top + 7, 314, r0.bottom - 7}, AccentColor(config_), 16);
        Text(dc, L"‹ / ›", {282, r0.top, 314, r0.bottom}, 14, TextColor(config_), false, DT_CENTER);
        SettingsCard(dc, 1, L"Интенсивность: " + std::to_wstring(config_.glassStrength) + L"   - / +", false, 276);
        SettingsCard(dc, 2, L"Непрозрачность: " + std::to_wstring(config_.opacity) + L"   - / +", false, 276);
        if (config_.glassRandomSeed) {
            SettingsCard(dc, 3, L"Сид: Авто (случайный при запуске)", false, 276);
        } else {
            SettingsCard(dc, 3, L"Сид: " + std::to_wstring(config_.glassSeed) + L"   - / +", false, 276);
        }
        SettingsToggle(dc, 4, L"Случайный сид при запуске", config_.glassRandomSeed);
        SettingsToggle(dc, 5, L"Включить стекло", config_.glass);
        SettingsCard(dc, 6, L"Сбросить настройки стекла", true);
        SettingsCard(dc, 7, L"\u2039 Назад", false);
        Text(dc,L"Esc — назад · клик слева −, справа +",{16,354,324,386},12,muted);
        return;
    }
        if(page==4) {
            SettingsHeader(dc,L"Кнопки в карточке");
            SettingsToggle(dc,0,L"Предыдущий трек",config_.compactPrevious);
            SettingsToggle(dc,1,L"Пауза / воспроизведение",config_.pause);
            SettingsToggle(dc,2,L"Следующий трек",config_.compactNext);
            SettingsCard(dc,3,L"Ширина свёрнутого: " + std::to_wstring(config_.compactWidth) + L"   − / +");
            SettingsCard(dc,4,L"Высота свёрнутого: " + std::to_wstring(config_.compactHeight) + L"   − / +");
            SettingsCard(dc,5,L"Высота с текстом: " + std::to_wstring(config_.compactLyricsHeight) + L"   − / +");
            SettingsCard(dc,6,L"‹ Основные настройки");
            return;
        }
        if(page==2) {
            SettingsHeader(dc,L"Своя тема");
            int colors[]={config_.customBackground,config_.customAccent,config_.customText,config_.customMuted};
            const wchar_t* names[]={L"Фон",L"Акцент",L"Основной текст",L"Вторичный текст"};
            for(int i=0;i<4;++i) {
                auto r=SettingRow(i);
                SettingsCard(dc,i,std::wstring(names[i])+L"  "+ColorHex(colors[i]),false,284);
                Box(dc,{290,r.top+6,320,r.bottom-6},static_cast<COLORREF>(colors[i]),6);
            }
            SettingsCard(dc,4,L"Применить свою тему",true);
            SettingsCard(dc,5,L"Скопировать текущие цвета");
            SettingsCard(dc,6,L"Сбросить свои цвета");
            SettingsCard(dc,7,L"‹ Оформление и текст");
            Paragraph(dc,L"Нажмите на цвет для выбора RGB.\nВыбранный цвет применяется сразу.\nПодберите контрастный текст к фону.",{16,304,324,382},muted);
            return;
        }
        if(page==3) {
            SettingsHeader(dc,L"Жидкое стекло");
            std::wstring labels[]={L"Эффект: "+std::wstring(config_.glass?L"вкл.":L"выкл."),
                L"Сила бликов: "+std::to_wstring(config_.glassStrength)+L"%  − / +",
                L"Цветовой оттенок: "+std::to_wstring(config_.glassTint)+L"%  − / +",
                L"Движение бликов: "+std::wstring(config_.glassAnimated?L"вкл.":L"выкл."),
                L"Скорость: "+std::to_wstring(config_.glassSpeed)+L"%  − / +",
                L"Непрозрачность: "+std::to_wstring(config_.opacity)+L"%  − / +",
                L"Сбросить эффект",L"‹ Оформление и текст"};
            for(int i=0;i<8;++i) {
                if(i==0) SettingsToggle(dc,i,L"Эффект",config_.glass);
                else if(i==3) SettingsToggle(dc,i,L"Движение бликов",config_.glassAnimated);
                else SettingsCard(dc,i,labels[i]);
            }
            SettingsCard(dc,8,L"Узор стекла  ›",true);
            SettingsCard(dc,9,L"Снегопад  ›",true);
            Text(dc,L"GDI-блики, без размытия рабочего стола",{16,354,324,386},12,muted);
            return;
        }
        if(page==1) {
            SettingsHeader(dc,L"Оформление и текст");
            const wchar_t* themes[]={L"Океан",L"Лаванда",L"Мята",L"Янтарь",L"Роза",L"Своя"};
            std::wstring labels[]={L"Тема: "+std::wstring(themes[std::clamp(config_.theme, 0, 5)])+L"  ‹ / ›",
                L"Настроить свою тему  ›",
                L"Жидкое стекло  ›",
                L"Свечение: "+std::to_wstring(config_.glow)+L"%  − / +",
                L"Скругление: "+std::to_wstring(config_.roundness)+L"  − / +",
                L"Текст: "+std::wstring(config_.sourceLyrics?L"из каталога LRCLIB":config_.asr?L"локальный Whisper":L"выключен")+L"  ›",
                L"Текст в свёрнутом виде: "+std::wstring(config_.compactLyrics?L"вкл.":L"выкл."),L"‹ Основные настройки"};
            for(int i=0;i<8;++i) {
                if(i==6) SettingsToggle(dc,i,L"Текст в свёрнутом виде",config_.compactLyrics);
                else SettingsCard(dc,i,labels[i],i==5);
            }
            Paragraph(dc,L"Каталог: поиск по названию и артисту в сети.\nWhisper: локально, слышит весь звук ПК.\nСтекло — блики, без размытия рабочего стола.",{16,298,324,382},muted);
            return;
        }
        if(page==6) {
            SettingsHeader(dc,L"Снегопад");
            const wchar_t* windNames[] = {L"Нет", L"Влево", L"Вправо"};
            SettingsToggle(dc,0,L"Включить снегопад",config_.snowfallEnabled);
            SettingsCard(dc,1,L"Количество: "+std::to_wstring(config_.snowfallCount)+L"  - / +");
            SettingsCard(dc,2,L"Скорость: "+std::to_wstring(config_.snowfallSpeed)+L"  - / +");
            SettingsCard(dc,3,L"Размер: "+std::to_wstring(config_.snowfallSize)+L" px  - / +");
            SettingsCard(dc,4,L"Непрозрачность: "+std::to_wstring(config_.snowfallOpacity)+L"%  - / +");
            SettingsCard(dc,5,L"Ветер: "+std::wstring(windNames[std::clamp(config_.snowfallWindDirection, 0, 2)])+L"  < / >");
            SettingsCard(dc,6,L"Сила ветра: "+std::to_wstring(config_.snowfallWindStrength)+L"%  - / +");
            SettingsCard(dc,7,L"Размытие: "+std::to_wstring(config_.snowfallBlur)+L" px  - / +");
            SettingsCard(dc,8,L"Сбросить настройки снегопада",true);
            SettingsCard(dc,9,L"‹ Назад",false);
            Text(dc,L"Esc — назад · клик слева −, справа +",{16,354,324,386},12,muted);
            return;
        }
                SettingsHeader(dc,L"Настройки");
        const wchar_t* labels[]={L"Обложка в карточке",L"Название в карточке",L"Исполнитель в карточке",L"Пауза в карточке",L"Разворачивать при наведении",L"Закрепить развёрнутым",L"Громкость источника",L"Локальное распознавание"};
        bool values[]={config_.cover,config_.title,config_.artist,config_.pause,config_.hover,config_.pinned,config_.volume,config_.asr};
        for(int i=0;i<8;++i) {
            if(i==3) SettingsCard(dc,i,L"Кнопки в карточке  >",true);
            else SettingsToggle(dc,i,labels[i],values[i]);
        }
        SettingsCard(dc,8,L"Задержка: " + std::to_wstring(config_.delay) + L" мс   − / +");
        SettingsCard(dc,9,L"Оформление и режим текста  ›",true);
        Text(dc,L"Esc — назад · клик слева −, справа +",{16,354,324,386},12,muted);
}

bool SettingsUI::HandleClick(HWND hWnd, POINT p, int& page) {
    if (page == 5) {
        for (int i = 0; i < 8; ++i) if (Hit(SettingRow(i), p)) {
            Config defaults; int direction = p.x < 170 ? -1 : 1;
            if (i == 0) config_.glassPattern = (config_.glassPattern + direction + 8) % 8;
            if (i == 1) config_.glassStrength = std::clamp(config_.glassStrength + direction * 5, 0, 100);
            if (i == 2) config_.opacity = std::clamp(config_.opacity + direction * 5, 45, 100);
            if (i == 3) {
                if (config_.glassRandomSeed) {
                    config_.glassRandomSeed = false;
                    config_.glassSeed = 12345;
                } else {
                    config_.glassSeed = static_cast<int>(std::clamp<long long>(
                        static_cast<long long>(config_.glassSeed) + direction * 1000LL, 0, 0x7FFFFFFF));
                }
            }
            if (i == 4) config_.glassRandomSeed = !config_.glassRandomSeed;
            if (i == 5) config_.glass = !config_.glass;
            if (i == 6) {
                config_.glassPattern = 0;
                config_.glassStrength = 50;
                config_.glassTint = 35;
                config_.glassSpeed = 30;
                config_.glassAnimated = true;
                config_.opacity = 100;
                config_.glassSeed = 0;
                config_.glassRandomSeed = true;
            }
            if (i == 7) { page = 3; InvalidateRect(hWnd, nullptr, FALSE); return true; }
            InvalidateRect(hWnd, nullptr, FALSE);
            return true;
        }
        return false;
    }
    if (page == 4) {
        for (int i = 0; i < 7; ++i) if (Hit(SettingRow(i), p)) {
            Config defaults; int direction = p.x < 170 ? -1 : 1;
            if (i == 0) config_.compactPrevious = !config_.compactPrevious;
            if (i == 1) config_.pause = !config_.pause;
            if (i == 2) config_.compactNext = !config_.compactNext;
            if (i == 3) config_.compactWidth = std::clamp(config_.compactWidth + direction * 20, 200, 900);
            if (i == 4) config_.compactHeight = std::clamp(config_.compactHeight + direction * 10, 50, 600);
            if (i == 5) config_.compactLyricsHeight = std::clamp(config_.compactLyricsHeight + direction * 10, 80, 600);
            if (i == 6) { page = 0; InvalidateRect(hWnd, nullptr, FALSE); return true; }
            InvalidateRect(hWnd, nullptr, FALSE);
            return true;
        }
        return false;
    }
    if (page == 2 || page == 3) {
        const int rowCount = page == 2 ? 8 : 10;
        for (int i = 0; i < rowCount; ++i) if (Hit(SettingRow(i), p)) {
            Config defaults; int direction = p.x < 170 ? -1 : 1;
            if (page == 2) {
                int* colors[] = {&config_.customBackground, &config_.customAccent, &config_.customText, &config_.customMuted};
                if (i < 4) ChooseThemeColor(hWnd, *colors[i]);
                if (i == 4) config_.theme = 5;
                if (i == 5) { config_.customBackground = BackgroundColor(config_); config_.customAccent = AccentColor(config_); config_.customText = TextColor(config_); config_.customMuted = MutedColor(config_); config_.theme = 5; }
                if (i == 6) { config_.customBackground = defaults.customBackground; config_.customAccent = defaults.customAccent; config_.customText = defaults.customText; config_.customMuted = defaults.customMuted; }
            } else {
                if (i == 0) config_.glass = !config_.glass;
                if (i == 1) config_.glassStrength = std::clamp(config_.glassStrength + direction * 5, 0, 100);
                if (i == 2) config_.glassTint = std::clamp(config_.glassTint + direction * 5, 0, 100);
                if (i == 3) config_.glassAnimated = !config_.glassAnimated;
                if (i == 4) config_.glassSpeed = std::clamp(config_.glassSpeed + direction * 5, 0, 100);
                if (i == 5) config_.opacity = std::clamp(config_.opacity + direction * 5, 45, 100);
                if (i == 8) { page = 5; InvalidateRect(hWnd, nullptr, FALSE); return true; }
                if (i == 9) { page = 6; InvalidateRect(hWnd, nullptr, FALSE); return true; }
                if (i == 6) { config_.glass = defaults.glass; config_.glassStrength = defaults.glassStrength; config_.glassTint = defaults.glassTint; config_.glassAnimated = defaults.glassAnimated; config_.glassSpeed = defaults.glassSpeed; config_.opacity = defaults.opacity; }
            }
            if (i == 7) { page = 1; InvalidateRect(hWnd, nullptr, FALSE); return true; }
            InvalidateRect(hWnd, nullptr, FALSE);
            return true;
        }
        return false;
    }
    if (page == 6) {
        for (int i = 0; i < 10; ++i) if (Hit(SettingRow(i), p)) {
            Config defaults; int direction = p.x < 170 ? -1 : 1;
            if (i == 0) config_.snowfallEnabled = !config_.snowfallEnabled;
            if (i == 1) config_.snowfallCount = std::clamp(config_.snowfallCount + direction * 10, 30, 150);
            if (i == 2) config_.snowfallSpeed = std::clamp(config_.snowfallSpeed + direction, 1, 10);
            if (i == 3) config_.snowfallSize = std::clamp(config_.snowfallSize + direction, 1, 5);
            if (i == 4) config_.snowfallOpacity = std::clamp(config_.snowfallOpacity + direction * 5, 10, 100);
            if (i == 5) {
                const int wind = std::clamp(config_.snowfallWindDirection, 0, 2);
                config_.snowfallWindDirection = (wind + direction + 3) % 3;
            }
            if (i == 6) config_.snowfallWindStrength = std::clamp(config_.snowfallWindStrength + direction * 5, 0, 100);
            if (i == 7) config_.snowfallBlur = std::clamp(config_.snowfallBlur + direction, 0, 3);
            if (i == 8) {
                config_.snowfallEnabled = defaults.snowfallEnabled;
                config_.snowfallCount = defaults.snowfallCount;
                config_.snowfallSpeed = defaults.snowfallSpeed;
                config_.snowfallSize = defaults.snowfallSize;
                config_.snowfallOpacity = defaults.snowfallOpacity;
                config_.snowfallWindDirection = defaults.snowfallWindDirection;
                config_.snowfallWindStrength = defaults.snowfallWindStrength;
                config_.snowfallBlur = defaults.snowfallBlur;
            }
            if (i == 9) { page = 3; InvalidateRect(hWnd, nullptr, FALSE); return true; }
            InvalidateRect(hWnd, nullptr, FALSE);
            return true;
        }
        return false;
    }
        if (page == 1) {
        for (int i = 0; i < 8; ++i) if (Hit(SettingRow(i), p)) {
            int direction = p.x < 170 ? -1 : 1;
            if (i == 0) config_.theme = (config_.theme + direction + 6) % 6;
            if (i == 1) page = 2;
            if (i == 2) page = 3;
            if (i == 3) config_.glow = std::clamp(config_.glow + direction * 5, 0, 100);
            if (i == 4) config_.roundness = std::clamp(config_.roundness + direction * 2, 0, 28);
            if (i == 5) { int mode = config_.sourceLyrics ? 1 : config_.asr ? 2 : 0; mode = (mode + 1) % 3; config_.sourceLyrics = mode == 1; config_.asr = mode == 2; }
            if (i == 6) config_.compactLyrics = !config_.compactLyrics;
            if (i == 7) page = 0;
            InvalidateRect(hWnd, nullptr, FALSE);
            return true;
        }
        return false;
    }
    const wchar_t* labels[] = {L"Обложка в карточке", L"Название в карточке", L"Исполнитель в карточке", L"Кнопки в карточке  >", L"Разворачивать при наведении", L"Закрепить развёрнутым", L"Громкость источника", L"Локальное распознавание"};
    bool* values[] = {&config_.cover, &config_.title, &config_.artist, &config_.pause, &config_.hover, &config_.pinned, &config_.volume, &config_.asr};
    for (int i = 0; i < 10; ++i) if (Hit(SettingRow(i), p)) {
        if (i == 3) { page = 4; InvalidateRect(hWnd, nullptr, FALSE); return true; }
        if (i == 9) { page = 1; InvalidateRect(hWnd, nullptr, FALSE); return true; }
        if (i < 8) { *values[i] = !*values[i]; if (i == 7) { if (config_.asr) config_.sourceLyrics = false; } }
        else config_.delay = std::clamp(config_.delay + (p.x < 170 ? -100 : 100), 0, 10000);
        InvalidateRect(hWnd, nullptr, FALSE);
        return true;
    }
    return false;
}
} // namespace media