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
constexpr int RowHeight = 12, RowStep = 15;
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
    if (skill == 0 || skill >= SkillTableSize) return "Ataque comum";
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

bool CMuHelper::Button(int x, int y, int w, int h, const char* label, bool active)
{
    const bool hover = Inside(x, y, w, h);
    if (m_Rendering) {
        const float shade = hover ? .55f : .35f;
        if (active) Rect(x, y, w, h, .1f, shade, .1f, .95f);
        else Rect(x, y, w, h, shade, shade, shade, .95f);
        TextCenter(x, y + 1, w, label);
        return false;
    }
    if (hover && m_Click) { PlayBuffer(25, 0, 0); m_Click = false; return true; }
    return false;
}

void CMuHelper::Check(int x, int y, const char* label, BYTE* flag)
{
    if (Button(x, y, RowHeight, RowHeight, *flag ? "X" : "", *flag != 0)) { *flag = *flag ? 0 : 1; MarkDirty(); }
    if (m_Rendering) TextLeft(x + RowHeight + 4, y + 1, label);
}

void CMuHelper::Stepper(int x, int y, const char* label, BYTE* value)
{
    if (m_Rendering) {
        char text[8];
        Platform::TextFormatSecure(text, "%d", (int)*value);
        TextLeft(x, y + 1, label);
        Rect(x + 60, y, 24, RowHeight, 0, 0, 0, .9f);
        TextCenter(x + 60, y + 1, 24, text);
    }
    if (Button(x + 88, y, 16, RowHeight, "-") && *value > 1) { --*value; MarkDirty(); }
    if (Button(x + 106, y, 16, RowHeight, "+") && *value < 8) { ++*value; MarkDirty(); }
}

void CMuHelper::DelayStepper(int x, int y, WORD* value)
{
    if (m_Rendering) {
        char text[16];
        Platform::TextFormatSecure(text, "%d s", (int)*value);
        Rect(x, y, 34, RowHeight, 0, 0, 0, .9f);
        TextCenter(x, y + 1, 34, text);
    }
    if (Button(x + 36, y, 14, RowHeight, "-") && *value > 0) { --*value; MarkDirty(); }
    if (Button(x + 52, y, 14, RowHeight, "+") && *value < MaxDelaySeconds) { ++*value; MarkDirty(); }
}

void CMuHelper::SkillSlot(int x, int y, int w, BYTE* skill)
{
    if (Button(x, y, w, RowHeight, SkillName(*skill))) {
        m_PopupTarget = skill;
        m_PopupX = PanelX - PopupWidth - 2;
        m_PopupY = y;
    }
}

void CMuHelper::RunPopup()
{
    int y = m_PopupY;
    bool picked = false;
    BYTE choice = 0;
    if (Button(m_PopupX, y, PopupWidth, PopupRowStep - 1, SkillName(0))) picked = true;
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
    int y = PanelY + 42;
    Stepper(ContentX, y, "Alcance", &m_Config.HuntingRange); y += RowStep + 4;
    Check(ContentX, y, "Retornar posi\xE7\xE3o original", &m_Config.ReturnToOriginalPos); y += RowStep + 4;
    if (m_Rendering) TextLeft(ContentX, y + 1, "B\xE1sica");
    SkillSlot(ContentX + 60, y, ContentWidth - 60, &m_Config.BasicSkillID); y += RowStep + 4;
    Check(ContentX, y, "Skill 1", &m_Config.Skill1Enabled);
    SkillSlot(ContentX + 60, y, ContentWidth - 60, &m_Config.Skill1ID); y += RowStep;
    if (m_Rendering) TextLeft(ContentX + 16, y + 1, "Delay");
    DelayStepper(ContentX + 60, y, &m_Config.Skill1Delay); y += RowStep + 4;
    Check(ContentX, y, "Skill 2", &m_Config.Skill2Enabled);
    SkillSlot(ContentX + 60, y, ContentWidth - 60, &m_Config.Skill2ID); y += RowStep;
    if (m_Rendering) TextLeft(ContentX + 16, y + 1, "Delay");
    DelayStepper(ContentX + 60, y, &m_Config.Skill2Delay); y += RowStep + 4;
    Check(ContentX, y, "Auto buff", &m_Config.AutoBuffEnabled); y += RowStep;
    for (BYTE& buff : m_Config.BuffSkillID) {
        SkillSlot(ContentX + 16, y, ContentWidth - 16, &buff);
        y += RowStep;
    }
}

void CMuHelper::RunPicking()
{
    int y = PanelY + 42;
    Stepper(ContentX, y, "Alcance", &m_Config.PickRange); y += RowStep + 4;
    Check(ContentX, y, "Coletar todos os itens", &m_Config.PickAll); y += RowStep;
    Check(ContentX, y, "Habilitar coleta dos itens abaixo?", &m_Config.PickSelected); y += RowStep;
    Check(ContentX + 12, y, "Jewels", &m_Config.PickJewels); y += RowStep;
    Check(ContentX + 12, y, "Excellent Item", &m_Config.PickExcellent); y += RowStep;
    Check(ContentX + 12, y, "Zen", &m_Config.PickZen); y += RowStep;
    Check(ContentX + 12, y, "Extra Item", &m_Config.PickExtra); y += RowStep + 4;

    const int fieldWidth = ContentWidth - 44;
    if (m_Rendering) {
        Rect(ContentX, y, fieldWidth, RowHeight, 0, 0, 0, .9f);
        char shown[MUHELPER_EXTRA_SIZE + 2];
        Platform::TextFormatSecure(shown, "%s%s", m_Text, m_TextFocus ? "_" : "");
        TextLeft(ContentX + 2, y + 1, shown);
    } else if (m_Click && Inside(ContentX, y, fieldWidth, RowHeight)) {
        m_TextFocus = true;
        m_Click = false;
    }
    if (Button(ContentX + fieldWidth + 4, y, 40, RowHeight, "Add")) AddExtra();
    y += RowStep + 2;

    for (int n = 0; n < MUHELPER_EXTRA_COUNT; ++n) {
        const char* extra = m_Config.ExtraItems[n];
        if (extra[0] == 0) break;
        if (Button(ContentX, y, ContentWidth, RowHeight, extra, m_SelectedExtra == n)) m_SelectedExtra = n;
        y += RowHeight + 1;
    }
    if (m_SelectedExtra >= 0 && Button(ContentX, y + 2, ContentWidth, RowHeight, "Remover selecionado")) RemoveExtra();
}

void CMuHelper::RunPanel()
{
    if (m_Rendering) {
        Rect(PanelX, PanelY, PanelWidth, PanelHeight, 0, 0, 0, .85f);
        m_dwTextColor = 0xFF00FFFF;
        TextCenter(PanelX, PanelY + 6, PanelWidth, "Mu Helper");
        m_dwTextColor = 0xFFFFFFFF;
    }
    const int tabWidth = (ContentWidth - 4) / 2;
    if (Button(ContentX, PanelY + 22, tabWidth, 14, "Ca\xE7" "ando", m_Tab == 0)) { m_Tab = 0; m_TextFocus = false; }
    if (Button(ContentX + tabWidth + 4, PanelY + 22, tabWidth, 14, "Coletando", m_Tab == 1)) m_Tab = 1;
    if (m_Tab == 0) RunHunting(); else RunPicking();
    const int footer = PanelY + PanelHeight - 20;
    if (Button(ContentX, footer, ContentWidth - 28, 14, "Resetar Config")) ResetConfig();
    if (Button(ContentX + ContentWidth - 24, footer, 24, 14, "X")) Toggle();
}

void CMuHelper::RunBar()
{
    // ponytail: rótulos de texto no lugar de ícones; trocar por texturas quando houver arte definida.
    if (Button(BarX, BarY, BarButton, BarButton, "H", m_Open)) Toggle();
    if (Button(BarX + BarButton + 2, BarY, BarButton, BarButton, m_Active ? "||" : ">", !m_Active)) SendStart(!m_Active);
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
    if (m_Open && UIState::HasRightPanel()) { m_Open = false; m_PopupTarget = nullptr; m_TextFocus = false; }

    const bool overBar = Inside(BarX, BarY, 2 * BarButton + 2, BarButton);
    const bool overPanel = m_Open && Inside(PanelX, PanelY, PanelWidth, PanelHeight);
    const bool overPopup = m_Open && m_PopupTarget != nullptr;
    if (!overBar && !overPanel && !overPopup) {
        if (MouseLButtonPush != 0) m_TextFocus = false;
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
