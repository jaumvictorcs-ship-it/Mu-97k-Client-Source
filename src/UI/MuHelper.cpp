#include "stdafx.h"
#include "Platform/Text.h"
#include "Platform/ITimer.h"
#include "UI/MuHelper.h"
#include "Core/Font.h"
#include "Core/Window.h"
#include "Net/Network.h"
#include "UI/UIState.h"

CMuHelper gMuHelper;

namespace {
constexpr int PanelX = 450, PanelY = 0, PanelWidth = 190, PanelHeight = 432;
constexpr int ContentX = PanelX + 6, ContentWidth = PanelWidth - 12;
constexpr int SlotWidth = 20, SlotHeight = 28;
constexpr int BarX = 5, BarY = 5, BarButton = 18;
constexpr int PopupWidth = 120, PopupRowStep = 12;
constexpr int SkillSlots = 60, SkillTableSize = 64, SkillEntrySize = 40, SkillListOffset = 0x57;
constexpr DWORD SaveDelayMs = 1100;
constexpr BYTE DefaultRange = 6;
constexpr WORD MaxDelaySeconds = 3600;

bool Inside(int x, int y, int w, int h)
{
    return MouseX >= x && MouseX < x + w && MouseY >= y && MouseY < y + h;
}

void Rect(int x, int y, int w, int h, float r, float g, float b, float a)
{
    EnableAlphaTest(true);
    glColor4f(r, g, b, a);
    GL_DrawRect((float)x, (float)y, (float)w, (float)h);
    GL_ResetState();
    glColor4f(1, 1, 1, 1);
}

// Caixa com borda dourada, no estilo da janela de referência.
void Frame(int x, int y, int w, int h, float r, float g, float b)
{
    Rect(x, y, w, h, .5f, .42f, .25f, 1);
    Rect(x + 1, y + 1, w - 2, h - 2, r, g, b, .95f);
}

void TextLeft(int x, int y, const char* text)
{
    RenderText(x, y, const_cast<char*>(text), 0, 0, nullptr);
}

void TextCenter(int x, int y, int w, const char* text)
{
    RenderText(x, y, const_cast<char*>(text), (int)(w * gWindow.GetWidth() / 640), 1, nullptr);
}

const char* SkillName(BYTE skill)
{
    if (skill == 0 || skill >= SkillTableSize) return "Nenhuma";
    return SkillAttribute.Raw + (skill * SkillEntrySize);
}

BYTE LearnedSkill(int slot)
{
    const char* attributes = (const char*)DAT_07cf1ff4;
    return attributes ? (BYTE)attributes[SkillListOffset + slot] : 0;
}
}

void CMuHelper::Clear()
{
    memset(&m_Config, 0, sizeof(m_Config));
    m_PopupTarget = nullptr;
    m_DelayFocus = nullptr;
    m_SelectedExtra = -1;
    m_Text[0] = 0;
    m_Open = m_Active = m_Dirty = m_TextFocus = false;
}

void CMuHelper::Toggle()
{
    if (!m_Open) {
        if (InventoryOpened) InventoryOpened = 0;
        if (UIState::HasRightPanel()) return;
    }
    m_Open = !m_Open;
    m_PopupTarget = nullptr;
    m_DelayFocus = nullptr;
    m_TextFocus = false;
    PlayBuffer(25, 0, 0);
}

void CMuHelper::ResetConfig()
{
    memset(&m_Config, 0, sizeof(m_Config));
    m_Config.HuntingRange = DefaultRange;
    m_Config.PickRange = DefaultRange;
    m_SelectedExtra = -1;
    MarkDirty();
}

void CMuHelper::MarkDirty()
{
    m_Dirty = true;
    m_DirtyTick = Platform::GetTimer().Ticks();
}

void CMuHelper::SendConfig()
{
#pragma pack(push, 1)
    struct { BYTE type, size, head, sub; MUHELPER_CONFIG config; } packet;
#pragma pack(pop)
    packet.type = 0xC1;
    packet.size = (BYTE)sizeof(packet);
    packet.head = 0xF3;
    packet.sub = 0xF0;
    packet.config = m_Config;
    gNetwork.SendC1((const BYTE*)&packet, sizeof(packet));
    m_Dirty = false;
}

void CMuHelper::SendStart(bool start)
{
    const BYTE packet[5] = { 0xC1, 5, 0xF3, 0xF1, (BYTE)(start ? 1 : 0) };
    gNetwork.SendC1(packet, sizeof(packet));
}

void CMuHelper::ReceiveConfig(const BYTE* packet, int size)
{
    if (size != 4 + (int)sizeof(MUHELPER_CONFIG) || m_Dirty) return;
    memcpy(&m_Config, packet + 4, sizeof(m_Config));
}

void CMuHelper::ReceiveState(const BYTE* packet, int size)
{
    if (size != 6) return;
    m_Active = packet[4] != 0;
}

void CMuHelper::AddExtra()
{
    if (m_Text[0] == 0) return;
    for (auto& extra : m_Config.ExtraItems) {
        if (extra[0] != 0) continue;
        Platform::TextCopySecure(extra, m_Text);
        m_Text[0] = 0;
        MarkDirty();
        return;
    }
}

void CMuHelper::RemoveExtra()
{
    if (m_SelectedExtra < 0 || m_SelectedExtra >= MUHELPER_EXTRA_COUNT) return;
    m_Config.ExtraItems[m_SelectedExtra][0] = 0;
    for (int n = m_SelectedExtra; n + 1 < MUHELPER_EXTRA_COUNT; ++n) {
        memcpy(m_Config.ExtraItems[n], m_Config.ExtraItems[n + 1], MUHELPER_EXTRA_SIZE);
    }
    memset(m_Config.ExtraItems[MUHELPER_EXTRA_COUNT - 1], 0, MUHELPER_EXTRA_SIZE);
    m_SelectedExtra = -1;
    MarkDirty();
}

bool CMuHelper::UpdateTextInput()
{
    if (m_Open && m_DelayFocus) {
        if (PressKey(VK_ESCAPE) || PressKey(VK_RETURN)) { m_DelayFocus = nullptr; return true; }
        if (PressKey(VK_BACK)) { *m_DelayFocus /= 10; MarkDirty(); return true; }
        for (int key = '0'; key <= '9'; ++key) {
            if (!PressKey(key)) continue;
            const int typed = *m_DelayFocus * 10 + (key - '0');
            *m_DelayFocus = (WORD)(typed > MaxDelaySeconds ? MaxDelaySeconds : typed);
            MarkDirty();
            break;
        }
        return true;
    }
    if (!m_Open || !m_TextFocus) return false;
    const int length = (int)strlen(m_Text);
    if (PressKey(VK_ESCAPE)) { m_TextFocus = false; return true; }
    if (PressKey(VK_RETURN)) { AddExtra(); return true; }
    if (PressKey(VK_BACK) && length > 0) { m_Text[length - 1] = 0; return true; }
    if (length >= MUHELPER_EXTRA_SIZE - 1) return true;
    char typed = 0;
    for (int key = 'A'; key <= 'Z' && !typed; ++key) if (PressKey(key)) typed = (char)key;
    for (int key = '0'; key <= '9' && !typed; ++key) if (PressKey(key)) typed = (char)key;
    if (!typed && PressKey(VK_SPACE)) typed = ' ';
    if (typed) { m_Text[length] = typed; m_Text[length + 1] = 0; }
    return true;
}

bool CMuHelper::Hit(int x, int y, int w, int h)
{
    if (m_Rendering || !m_Click || !Inside(x, y, w, h)) return false;
    PlayBuffer(25, 0, 0);
    m_Click = false;
    return true;
}

bool CMuHelper::Button(int x, int y, int w, int h, const char* label, bool active)
{
    if (m_Rendering) {
        const float shade = Inside(x, y, w, h) ? .5f : .3f;
        Frame(x, y, w, h, active ? .1f : shade, active ? shade : shade, active ? .1f : shade);
        TextCenter(x, y + (h - 10) / 2, w, label);
    }
    return Hit(x, y, w, h);
}

void CMuHelper::Check(int x, int y, const char* label, BYTE* flag)
{
    if (m_Rendering) {
        Frame(x, y, 12, 12, *flag ? .75f : .3f, .05f, .05f);
        if (*flag) TextCenter(x, y + 1, 12, "X");
        if (label[0]) TextLeft(x + 16, y + 1, label);
    }
    if (Hit(x, y, 12, 12)) { *flag = *flag ? 0 : 1; MarkDirty(); }
}

// Alcance: número grande e setas, como na imagem de referência.
void CMuHelper::Range(int y, BYTE* value)
{
    Section(y, 36);
    const int x = ContentX + 8;
    if (m_Rendering) {
        char text[8];
        Platform::TextFormatSecure(text, "%d", (int)*value);
        TextLeft(x, y + 4, "Alcance");
        HGDIOBJ font = gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_BOLD));
        TextCenter(x, y + 18, 40, text);
        gFont.SelectFont(gFont.GetTextDC(), font);
    }
    if (Arrow(x + 64, y + 5, true) && *value < 8) { ++*value; MarkDirty(); }
    if (Arrow(x + 64, y + 20, false) && *value > 1) { --*value; MarkDirty(); }
}

bool CMuHelper::Arrow(int x, int y, bool up)
{
    if (m_Rendering) {
        Frame(x, y, 12, 12, .4f, .3f, .1f);
        for (int row = 0; row < 4; ++row) {
            const int half = up ? row : 3 - row;
            Rect(x + 6 - half - 1, y + 4 + row, 2 * half + 2, 1, .95f, .8f, .35f, 1);
        }
    }
    return Hit(x, y, 12, 12);
}

void CMuHelper::Delay(int x, int y, BYTE* enabled, WORD* value)
{
    Check(x, y + 8, "", enabled);
    if (m_Rendering) {
        char text[8];
        const bool focus = m_DelayFocus == value;
        Platform::TextFormatSecure(text, "%d%s", (int)*value, focus ? "_" : "");
        TextLeft(x + 15, y + 9, "Delay");
        Frame(x + 42, y + 7, 30, 13, 0, focus ? .25f : .1f, focus ? .35f : .2f);
        TextCenter(x + 42, y + 8, 30, text);
        TextCenter(x + 42, y + 21, 30, "Sec");
    }
    if (Hit(x + 42, y + 7, 30, 13)) { m_DelayFocus = value; m_TextFocus = false; }
}

void CMuHelper::SkillSlot(int x, int y, BYTE* skill)
{
    if (m_Rendering) {
        Frame(x - 1, y - 1, SlotWidth + 2, SlotHeight + 2, .03f, .03f, .03f);
        for (int slot = 0; *skill != 0 && slot < SkillSlots; ++slot) {
            if (LearnedSkill(slot) != *skill) continue;
            glColor4f(1, 1, 1, 1);
            EnableAlphaTest(true);
            RenderSkillIcon(slot, (float)x, (float)y, (float)SlotWidth, (float)SlotHeight);
            GL_ResetState();
            break;
        }
        if (*skill != 0 && !m_PopupTarget && Inside(x, y, SlotWidth, SlotHeight)) {
            Rect(PanelX - PopupWidth - 2, y, PopupWidth, 13, 0, 0, 0, .9f);
            TextCenter(PanelX - PopupWidth - 2, y + 1, PopupWidth, SkillName(*skill));
        }
    }
    if (Hit(x, y, SlotWidth, SlotHeight)) {
        m_PopupTarget = skill;
        m_PopupX = PanelX - PopupWidth - 2;
        m_PopupY = y;
    }
}

void CMuHelper::Section(int y, int h)
{
    if (m_Rendering) Frame(ContentX, y, ContentWidth, h, .1f, .1f, .11f);
}

void CMuHelper::RunPopup()
{
    int y = m_PopupY;
    bool picked = false;
    BYTE choice = 0;
    if (Button(m_PopupX, y, PopupWidth, PopupRowStep - 1, "Nenhuma")) picked = true;
    y += PopupRowStep;
    for (int slot = 0; slot < SkillSlots; ++slot) {
        const BYTE skill = LearnedSkill(slot);
        if (skill == 0 || skill >= SkillTableSize) continue;
        if (Button(m_PopupX, y, PopupWidth, PopupRowStep - 1, SkillName(skill))) { picked = true; choice = skill; }
        y += PopupRowStep;
    }
    if (m_Rendering) return;
    if (picked) { *m_PopupTarget = choice; MarkDirty(); m_PopupTarget = nullptr; return; }
    if (m_Click) { m_PopupTarget = nullptr; m_Click = false; }
}

void CMuHelper::RunHunting()
{
    const int x = ContentX;
    Range(PanelY + 40, &m_Config.HuntingRange);

    Section(PanelY + 80, 22);
    Check(x + 8, PanelY + 85, "Retornar posi\xE7\xE3o original", &m_Config.ReturnToOriginalPos);

    const int skills = PanelY + 106;
    Section(skills, 110);
    if (m_Rendering) {
        TextLeft(x + 8, skills + 4, "B\xE1sica");
        TextLeft(x + 48, skills + 4, "Skill 1");
        TextLeft(x + 48, skills + 56, "Skill 2");
    }
    SkillSlot(x + 10, skills + 18, &m_Config.BasicSkillID);
    SkillSlot(x + 50, skills + 18, &m_Config.Skill1ID);
    Delay(x + 80, skills + 18, &m_Config.Skill1Enabled, &m_Config.Skill1Delay);
    SkillSlot(x + 50, skills + 70, &m_Config.Skill2ID);
    Delay(x + 80, skills + 70, &m_Config.Skill2Enabled, &m_Config.Skill2Delay);

    const int buffs = PanelY + 220;
    Section(buffs, 56);
    Check(x + 8, buffs + 5, "Auto buff", &m_Config.AutoBuffEnabled);
    for (int n = 0; n < 3; ++n) SkillSlot(x + 40 + n * 38, buffs + 22, &m_Config.BuffSkillID[n]);
}

void CMuHelper::RunPicking()
{
    const int x = ContentX;
    Range(PanelY + 40, &m_Config.PickRange);

    Section(PanelY + 80, 22);
    Check(x + 8, PanelY + 85, "Coletar todos os itens", &m_Config.PickAll);

    int y = PanelY + 106;
    Section(y, 256);
    Check(x + 8, y + 5, "Habilitar coleta dos itens abaixo?", &m_Config.PickSelected);
    y += 24;
    Check(x + 8, y, "Jewels", &m_Config.PickJewels);
    Check(x + 58, y, "Excellent Item", &m_Config.PickExcellent);
    Check(x + 138, y, "Zen", &m_Config.PickZen);
    y += 16;
    Check(x + 8, y, "Extra Item", &m_Config.PickExtra);
    y += 18;

    const int fieldX = x + 8, fieldWidth = ContentWidth - 16 - 38;
    if (m_Rendering) {
        Frame(fieldX, y, fieldWidth, 14, 0, .1f, .2f);
        char shown[MUHELPER_EXTRA_SIZE + 2];
        Platform::TextFormatSecure(shown, "%s%s", m_Text, m_TextFocus ? "_" : "");
        TextLeft(fieldX + 2, y + 2, shown);
    }
    if (Hit(fieldX, y, fieldWidth, 14)) { m_TextFocus = true; m_DelayFocus = nullptr; }
    if (Button(fieldX + fieldWidth + 4, y, 34, 14, "Add")) AddExtra();
    y += 18;

    if (m_Rendering) Frame(fieldX, y, ContentWidth - 16, 160, .05f, .05f, .06f);
    for (int n = 0; n < MUHELPER_EXTRA_COUNT; ++n) {
        const char* extra = m_Config.ExtraItems[n];
        if (extra[0] == 0) break;
        if (Button(fieldX + 2, y + 2 + n * 13, ContentWidth - 20, 12, extra, m_SelectedExtra == n)) m_SelectedExtra = n;
    }
    if (m_SelectedExtra >= 0 && Button(fieldX, y + 164, ContentWidth - 16, 13, "Remover selecionado")) RemoveExtra();
}

void CMuHelper::RunPanel()
{
    if (m_Rendering) {
        Frame(PanelX, PanelY, PanelWidth, PanelHeight, .06f, .06f, .07f);
        m_dwTextColor = 0xFF80D0F0;
        TextCenter(PanelX, PanelY + 6, PanelWidth, "Mu Helper");
        Rect(ContentX, PanelY + 20, ContentWidth, 1, .55f, .45f, .25f, 1);
        m_dwTextColor = 0xFFFFFFFF;
        TextCenter(ContentX, PanelY + 24, 60, "Ca\xE7" "ando");
        TextCenter(ContentX + 60, PanelY + 24, 60, "Coletando");
        Rect(ContentX + 8 + m_Tab * 60, PanelY + 36, 44, 1, .85f, .65f, .25f, 1);
    }
    if (Hit(ContentX, PanelY + 22, 60, 14)) { m_Tab = 0; m_TextFocus = false; }
    if (Hit(ContentX + 60, PanelY + 22, 60, 14)) { m_Tab = 1; m_DelayFocus = nullptr; }
    if (m_Tab == 0) RunHunting(); else RunPicking();
    const int footer = PanelY + PanelHeight - 20;
    if (Button(PanelX + (PanelWidth - 90) / 2, footer - 20, 90, 15, "Resetar Config")) ResetConfig();
    if (Button(ContentX, footer, 18, 15, "X")) Toggle();
}

void CMuHelper::RunBar()
{
    if (m_Rendering) {
        const int size = BarButton;
        Frame(BarX, BarY, size, size, Inside(BarX, BarY, size, size) ? .3f : .15f, .15f, .15f);
        static const char* const gear[12] = {
            "....#..#....", ".##.####.##.", ".##########.", "..###..###..",
            "####....####", "###......###", "###......###", "####....####",
            "..###..###..", ".##########.", ".##.####.##.", "....#..#...." };
        for (int row = 0; row < 12; ++row) {
            for (int col = 0; col < 12; ++col) {
                if (gear[row][col] == '#') Rect(BarX + 3 + col, BarY + 3 + row, 1, 1, .85f, .85f, .85f, 1);
            }
        }
        const int play = BarX + size + 2;
        Frame(play, BarY, size, size, Inside(play, BarY, size, size) ? .3f : .15f, .15f, .15f);
        if (m_Active) {
            Rect(play + 5, BarY + 4, 3, 10, .95f, .8f, .2f, 1);
            Rect(play + 10, BarY + 4, 3, 10, .95f, .8f, .2f, 1);
        } else {
            for (int row = 0; row < 11; ++row) {
                const int len = (row <= 5 ? row : 10 - row) * 2 + 1;
                Rect(play + 5, BarY + 4 + row, len, 1, .2f, .85f, .2f, 1);
            }
        }
    }
    if (Hit(BarX, BarY, BarButton, BarButton)) Toggle();
    if (Hit(BarX + BarButton + 2, BarY, BarButton, BarButton)) SendStart(!m_Active);
}

void CMuHelper::Run()
{
    RunBar();
    if (!m_Open) return;
    if (!m_Rendering && m_PopupTarget) { RunPopup(); return; }
    RunPanel();
    if (m_Rendering && m_PopupTarget) RunPopup();
}

void CMuHelper::UpdateMouse()
{
    if (SceneFlag != 5) return;
    if (m_Dirty && (Platform::GetTimer().Ticks() - m_DirtyTick) >= SaveDelayMs) SendConfig();
    if (m_Open && UIState::HasRightPanel()) { m_Open = false; m_PopupTarget = nullptr; m_DelayFocus = nullptr; m_TextFocus = false; }

    const bool overBar = Inside(BarX, BarY, 2 * BarButton + 2, BarButton);
    const bool overPanel = m_Open && Inside(PanelX, PanelY, PanelWidth, PanelHeight);
    const bool overPopup = m_Open && m_PopupTarget != nullptr;
    if (!overBar && !overPanel && !overPopup) {
        if (MouseLButtonPush != 0) { m_TextFocus = false; m_DelayFocus = nullptr; }
        return;
    }

    UIState::CaptureMouseForUI();
    m_Click = MouseLButtonPush != 0;
    MouseLButton = 0;
    MouseLButtonPush = 0;
    MouseLButtonPop = 0;
    m_Rendering = false;
    Run();
    m_Click = false;
}

void CMuHelper::Render()
{
    if (SceneFlag != 5) return;
    const DWORD color = m_dwTextColor, back = m_dwBackColor;
    HGDIOBJ font = gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
    m_dwBackColor = 0;
    m_dwTextColor = 0xFFFFFFFF;
    m_Rendering = true;
    Run();
    m_Rendering = false;
    gFont.SelectFont(gFont.GetTextDC(), font);
    m_dwTextColor = color;
    m_dwBackColor = back;
    glColor4f(1, 1, 1, 1);
}
