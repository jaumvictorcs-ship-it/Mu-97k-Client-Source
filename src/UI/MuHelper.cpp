#include "stdafx.h"
#include "Platform/Text.h"
#include "Platform/ITimer.h"
#include "UI/MuHelper.h"
#include "Core/Font.h"
#include "Core/Window.h"
#include "Net/Network.h"
#include "UI/UIState.h"
#include "Entity/EntityTypes.h"
#include <cmath>

extern "C" void DbgLogPublic(const char* msg);

CMuHelper gMuHelper;

namespace {
constexpr int PanelX = 450, PanelY = 0, PanelWidth = 190, PanelHeight = 433;
constexpr int ContentX = PanelX + 10, ContentWidth = PanelWidth - 20;
// Celula do atlas de skills, a mesma do HUD nativo (HUD_Pass5).
constexpr int SlotWidth = 32, SlotHeight = 36;
// Texturas de interface carregadas pelo proprio jogo.
constexpr int PanelTopTexture = 260, PanelBottomTexture = 261, BoxTexture = 240, SlotTexture = 0x115;
constexpr int CloseBackTexture = 0x118, CloseTexture = 280, CloseHoverTexture = 281, ArrowTexture = 0x504;
constexpr int ArrowSize = 13;
constexpr float ArrowUV = 13.0f / 16.0f;
constexpr int BarX = 5, BarY = 5, BarButton = 22;
constexpr int PopupWidth = 120, PopupRowStep = 15;
constexpr int SkillSlots = 60, SkillTableSize = 64, SkillEntrySize = 40, SkillListOffset = 0x57;
constexpr DWORD SaveDelayMs = 1100;
constexpr BYTE DefaultRange = 6;
constexpr int RingSegments = 64;
constexpr WORD NoAttackKey = 0xFFFF;
constexpr DWORD ManualGraceMs = 400;
constexpr int MaxEntities = 400, EntityStride = 0x394, EntityKeyOffset = 0x1DC, EntityDeadOffset = 0x2FD;
constexpr int HeroSkillSlotOffset = 913;
constexpr BYTE MaxWalkSteps = 14;
constexpr float RingWidth = 16.0f;
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

// Caixa com borda dourada, no estilo da janela de referÃªncia.
void Frame(int x, int y, int w, int h, float r, float g, float b)
{
    Rect(x, y, w, h, .5f, .42f, .25f, 1);
    Rect(x + 1, y + 1, w - 2, h - 2, r, g, b, .95f);
}

void Texture(int id, int x, int y, int w, int h, float du, float v, float dv)
{
    GL_ResetState();
    glColor4f(1, 1, 1, 1);
    GL_DrawTexture(id, (float)x, (float)y, (float)w, (float)h, 0.0f, v, du, dv, 1, 1);
}

// Caixa com a moldura dourada nativa (Message_box); com o mouse em cima, realce aditivo como no menu de opcoes.
void Box(int x, int y, int w, int h, bool hoverable, bool active)
{
    GL_ResetState();
    if (active) glColor3f(.8f, .6f, .4f);
    else glColor3f(1, 1, 1);
    GL_DrawTexture(BoxTexture, (float)x, (float)y, (float)w, (float)h, 0.0f, 0.0f, 213.0f / 256.0f, 1.0f, 1, 1);
    if (hoverable && Inside(x, y, w, h)) {
        glColor3f(.8f, .6f, .4f);
        GL_SetBlendAdditive();
        GL_DrawTexture(BoxTexture, (float)x, (float)y, (float)w, (float)h, 0.0f, 0.0f, 213.0f / 256.0f, 1.0f, 1, 1);
        GL_ResetState();
    }
    glColor4f(1, 1, 1, 1);
}

void TextLeft(int x, int y, const char* text)
{
    EnableAlphaTest(true);
    RenderText(x, y, const_cast<char*>(text), 0, 0, nullptr);
}

void TextCenter(int x, int y, int w, const char* text)
{
    EnableAlphaTest(true);
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
    ReleaseAttack();
    m_Manual = false;
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
    const bool active = packet[4] != 0;
    m_Active = active;
    if (!active) { ReleaseAttack(); m_Manual = false; }
    char line[64];
    Platform::TextFormatSecure(line, "MuHelper: estado active=%d reason=%d", (int)packet[4], (int)packet[5]);
    DbgLogPublic(line);
}

// O servidor escolhe o destino; o cliente anda como em um clique no chão.
void CMuHelper::ReceiveMove(const BYTE* packet, int size)
{
    if (size != 7 || !Hero || m_Manual) return;
    ReleaseAttack();
    CHARACTER* hero = AsCharacter(Hero);
    const int x = packet[4], y = packet[5];
    if (hero->Movement && hero->TargetX == x && hero->TargetY == y) return;
    const unsigned int found = Path_FindRoute(hero->PositionX, hero->PositionY, x, y, (unsigned char*)Hero + 852, (float)packet[6]);
    char line[80];
    Platform::TextFormatSecure(line, "MuHelper: andar (%d,%d)->(%d,%d) reach=%d rota=%u", hero->PositionX, hero->PositionY, x, y, (int)packet[6], found);
    DbgLogPublic(line);
    if (!found) return;
    hero->TargetX = (BYTE)x;
    hero->TargetY = (BYTE)y;
    hero->MovementType = 0;
    // O pacote de movimento nativo leva até 14 passos: o restante da rota vem no próximo comando.
    if (hero->PathNum > MaxWalkSteps) hero->PathNum = MaxWalkSteps;
    Combat_SendMovePathPacket((int)(uintptr_t)Hero, (int)(uintptr_t)&hero->Object);
}

void CMuHelper::ReceiveAttack(const BYTE* packet, int size)
{
    if (size != 7 || m_Manual) return;
    m_AttackKey = (WORD)((packet[4] << 8) | packet[5]);
    m_AttackSkill = packet[6];
    if (m_AttackKey == NoAttackKey) ReleaseAttack();
}

void CMuHelper::ReleaseAttack()
{
    m_AttackKey = NoAttackKey;
    if (!m_Attacking) return;
    m_Attacking = false;
    m_bAutoAttack = m_SavedAutoAttack;
    SelectedCharacter = -1;
    Attacking = -1;
}

// Marca o alvo como no auto-ataque do jogador: o resto é o combate nativo do cliente.
void CMuHelper::SendManual(bool active)
{
    const BYTE packet[5] = { 0xC1, 5, 0xF3, 0xF2, (BYTE)(active ? 1 : 0) };
    gNetwork.SendC1(packet, sizeof(packet));
}

// O clique do jogador tem prioridade: sem soltar o alvo, o clique no chao cairia no ramo de atacar.
void CMuHelper::UpdateManual()
{
    const DWORD now = Platform::GetTimer().Ticks();
    const bool clicking = DAT_07d78094 == 0 && (DAT_083a4124 != 0 || DAT_083a42c4 != 0);
    if (clicking) {
        m_ManualTick = now;
        if (!m_Manual) {
            m_Manual = true;
            ReleaseAttack();
            SendManual(true);
        }
        return;
    }
    if (m_Manual && !AsCharacter(Hero)->Movement && now - m_ManualTick >= ManualGraceMs) {
        m_Manual = false;
        SendManual(false);
    }
}

void CMuHelper::UpdateAttack()
{
    if (!m_Active || !Hero) return;
    UpdateManual();
    if (m_Manual || m_AttackKey == NoAttackKey) return;
    BYTE* const base = (BYTE*)(uintptr_t)DAT_07abf5d0;
    int target = -1;
    for (int n = 0; n < MaxEntities && target < 0; ++n) {
        const BYTE* entity = base + n * EntityStride;
        if (entity[0] && *(const WORD*)(entity + EntityKeyOffset) == m_AttackKey) target = n;
    }
    if (target < 0 || base[target * EntityStride + EntityDeadOffset] != 0) { ReleaseAttack(); return; }

    if (!m_Attacking) {
        m_SavedAutoAttack = m_bAutoAttack;
        m_Attacking = true;
    }
    m_bAutoAttack = 1;

    int skillSlot = -1;
    for (int slot = 0; m_AttackSkill != 0 && slot < SkillSlots && skillSlot < 0; ++slot) {
        if (LearnedSkill(slot) == m_AttackSkill) skillSlot = slot;
    }
    if (skillSlot >= 0) *(BYTE*)(Hero + HeroSkillSlotOffset) = (BYTE)skillSlot;
    SelectedCharacter = target;
    Attacking = (skillSlot >= 0) ? 2 : 1;   // 2 = clique direito (skill), 1 = clique esquerdo (ataque comum)
}

// Círculo no chão: mostra o raio da aba aberta; com a janela fechada, o raio de caça.
void CMuHelper::RenderWorld()
{
    if (SceneFlag != 5 || !Hero || (!m_Open && !m_Active)) return;
    const bool picking = m_Open && m_Tab == 1;
    const float radius = (float)(picking ? m_Config.PickRange : m_Config.HuntingRange) * 100.0f;
    if (radius <= 0.0f) return;
    const CHARACTER* hero = AsCharacter(Hero);
    const float cx = (float)hero->PositionX * 100.0f + 50.0f;
    const float cy = (float)hero->PositionY * 100.0f + 50.0f;

    GL_SetBlendAdditive();
    GL_SetAlphaTest('\0');
    GL_DisableDepthTest();
    const float red = picking ? .25f : 1.0f, green = picking ? 1.0f : .35f, blue = picking ? .35f : .15f;
    // Duas faixas: o centro brilha e as bordas somem, sem linha dura no chao.
    for (int band = 0; band < 2; ++band) {
        glBegin(GL_QUAD_STRIP);
        for (int n = 0; n <= RingSegments; ++n) {
            const float angle = 6.2831853f * (float)n / (float)RingSegments;
            for (int edge = 0; edge < 2; ++edge) {
                const float r = radius + (float)(band + edge - 1) * RingWidth;
                const float glow = (band + edge == 1) ? 1.0f : 0.0f;
                const float x = cx + cosf(angle) * r;
                const float y = cy + sinf(angle) * r;
                glColor3f(red * glow, green * glow, blue * glow);
                glVertex3f(x, y, RequestTerrainHeight(x, y) + 8.0f);
            }
        }
        glEnd();
    }
    GL_EnableDepthTest();
    GL_ResetState();
    glColor4f(1, 1, 1, 1);
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
        Box(x, y, w, h, true, active);
        TextCenter(x, y + (h - 10) / 2, w, label);
    }
    return Hit(x, y, w, h);
}

void CMuHelper::Check(int x, int y, const char* label, BYTE* flag)
{
    if (m_Rendering) {
        Frame(x, y, 12, 12, *flag ? .75f : .25f, .05f, .05f);
        if (*flag) TextCenter(x, y + 1, 12, "X");
        if (label[0]) TextLeft(x + 16, y + 1, label);
    }
    if (Hit(x, y, 12, 12)) { *flag = *flag ? 0 : 1; MarkDirty(); }
}

// Alcance: numero grande e setas, como na imagem de referencia.
void CMuHelper::Range(int y, BYTE* value)
{
    Section(y, 40);
    const int x = ContentX + 10;
    if (m_Rendering) {
        char text[8];
        Platform::TextFormatSecure(text, "%d", (int)*value);
        TextLeft(x, y + 6, "Alcance");
        HGDIOBJ font = gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_BOLD));
        TextCenter(x, y + 21, 40, text);
        gFont.SelectFont(gFont.GetTextDC(), font);
    }
    if (Arrow(x + 62, y + 6, true) && *value < 8) { ++*value; MarkDirty(); }
    if (Arrow(x + 62, y + 22, false) && *value > 1) { --*value; MarkDirty(); }
}

bool CMuHelper::Arrow(int x, int y, bool up)
{
    if (m_Rendering) {
        Texture(ArrowTexture, x, y, ArrowSize, ArrowSize, ArrowUV, up ? 0.0f : ArrowUV, up ? ArrowUV : -ArrowUV);
        if (Inside(x, y, ArrowSize, ArrowSize)) {
            GL_SetBlendAdditive();
            GL_DrawTexture(ArrowTexture, (float)x, (float)y, (float)ArrowSize, (float)ArrowSize, 0.0f, up ? 0.0f : ArrowUV, ArrowUV, up ? ArrowUV : -ArrowUV, 1, 1);
            GL_ResetState();
        }
    }
    return Hit(x, y, ArrowSize, ArrowSize);
}

void CMuHelper::Delay(int x, int y, BYTE* enabled, WORD* value)
{
    Check(x, y + 11, "", enabled);
    if (m_Rendering) {
        char text[8];
        const bool focus = m_DelayFocus == value;
        Platform::TextFormatSecure(text, "%d%s", (int)*value, focus ? "_" : "");
        TextLeft(x + 15, y + 12, "Delay");
        Frame(x + 44, y + 10, 26, 13, 0, focus ? .25f : .08f, focus ? .35f : .14f);
        TextCenter(x + 44, y + 11, 26, text);
        TextCenter(x + 44, y + 25, 26, "Sec");
    }
    if (Hit(x + 44, y + 10, 26, 13)) { m_DelayFocus = value; m_TextFocus = false; }
}

void CMuHelper::SkillSlot(int x, int y, BYTE* skill)
{
    if (m_Rendering) {
        Texture(SlotTexture, x, y, SlotWidth, SlotHeight, 1.0f, 0.0f, 1.0f);
        for (int slot = 0; *skill != 0 && slot < SkillSlots; ++slot) {
            if (LearnedSkill(slot) != *skill) continue;
            GL_ResetState();
            glColor4f(1, 1, 1, 1);
            RenderSkillIcon(slot, (float)x, (float)y, (float)SlotWidth, (float)SlotHeight);
            GL_ResetState();
            glColor4f(1, 1, 1, 1);
            break;
        }
        if (*skill != 0 && !m_PopupTarget && Inside(x, y, SlotWidth, SlotHeight)) {
            Box(PanelX - PopupWidth - 2, y, PopupWidth, 15, false, false);
            TextCenter(PanelX - PopupWidth - 2, y + 2, PopupWidth, SkillName(*skill));
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
    if (m_Rendering) Box(ContentX, y, ContentWidth, h, false, false);
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
    Range(PanelY + 52, &m_Config.HuntingRange);

    Section(PanelY + 96, 24);
    Check(x + 10, PanelY + 102, "Retornar posi\xE7\xE3o original", &m_Config.ReturnToOriginalPos);

    const int skills = PanelY + 124;
    Section(skills, 118);
    if (m_Rendering) {
        TextLeft(x + 10, skills + 6, "B\xE1sica");
        TextLeft(x + 50, skills + 6, "Skill 1");
        TextLeft(x + 50, skills + 62, "Skill 2");
    }
    SkillSlot(x + 10, skills + 20, &m_Config.BasicSkillID);
    SkillSlot(x + 50, skills + 20, &m_Config.Skill1ID);
    Delay(x + 90, skills + 20, &m_Config.Skill1Enabled, &m_Config.Skill1Delay);
    SkillSlot(x + 50, skills + 76, &m_Config.Skill2ID);
    Delay(x + 90, skills + 76, &m_Config.Skill2Enabled, &m_Config.Skill2Delay);

    const int buffs = PanelY + 246;
    Section(buffs, 68);
    Check(x + 10, buffs + 6, "Auto buff", &m_Config.AutoBuffEnabled);
    for (int n = 0; n < 3; ++n) SkillSlot(x + 24 + n * 44, buffs + 24, &m_Config.BuffSkillID[n]);
}

void CMuHelper::RunPicking()
{
    const int x = ContentX;
    Range(PanelY + 52, &m_Config.PickRange);

    const BYTE pickAll = m_Config.PickAll, pickSelected = m_Config.PickSelected;
    Section(PanelY + 96, 24);
    Check(x + 10, PanelY + 102, "Coletar todos os itens", &m_Config.PickAll);

    int y = PanelY + 124;
    Section(y, 250);
    Check(x + 10, y + 7, "Habilitar coleta dos itens abaixo?", &m_Config.PickSelected);
    y += 26;
    const BYTE filters = (BYTE)(m_Config.PickJewels + m_Config.PickExcellent + m_Config.PickZen + m_Config.PickExtra);
    Check(x + 10, y, "Jewels", &m_Config.PickJewels);
    Check(x + 56, y, "Excellent Item", &m_Config.PickExcellent);
    Check(x + 134, y, "Zen", &m_Config.PickZen);
    y += 16;
    Check(x + 10, y, "Extra Item", &m_Config.PickExtra);
    y += 18;

    // As opcoes sao excludentes: marcar um filtro significa coletar so o que foi marcado.
    if (!m_Rendering) {
        const BYTE filtersNow = (BYTE)(m_Config.PickJewels + m_Config.PickExcellent + m_Config.PickZen + m_Config.PickExtra);
        if (filtersNow > filters || (m_Config.PickSelected && !pickSelected)) { m_Config.PickSelected = 1; m_Config.PickAll = 0; }
        else if (m_Config.PickAll && !pickAll) m_Config.PickSelected = 0;
    }

    const int fieldX = x + 10, fieldWidth = ContentWidth - 20 - 38;
    if (m_Rendering) {
        Frame(fieldX, y, fieldWidth, 14, 0, .06f, .12f);
        char shown[MUHELPER_EXTRA_SIZE + 2];
        Platform::TextFormatSecure(shown, "%s%s", m_Text, m_TextFocus ? "_" : "");
        TextLeft(fieldX + 2, y + 2, shown);
    }
    if (Hit(fieldX, y, fieldWidth, 14)) { m_TextFocus = true; m_DelayFocus = nullptr; }
    if (Button(fieldX + fieldWidth + 4, y, 34, 14, "Add")) AddExtra();
    y += 18;

    if (m_Rendering) Frame(fieldX, y, ContentWidth - 20, 142, .04f, .04f, .05f);
    for (int n = 0; n < MUHELPER_EXTRA_COUNT; ++n) {
        const char* extra = m_Config.ExtraItems[n];
        if (extra[0] == 0) break;
        if (Button(fieldX + 2, y + 2 + n * 11, ContentWidth - 24, 11, extra, m_SelectedExtra == n)) m_SelectedExtra = n;
    }
    if (m_SelectedExtra >= 0 && Button(fieldX, y + 145, ContentWidth - 20, 14, "Remover selecionado")) RemoveExtra();
}

void CMuHelper::RunPanel()
{
    if (m_Rendering) {
        GL_ResetState();
        glColor4f(1, 1, 1, 1);
        GL_DrawTexture(PanelTopTexture, (float)PanelX, (float)PanelY, 190.0f, 256.0f, 0.0f, 0.0f, 0.7421875f, 1.0f, 1, 1);
        GL_DrawTexture(PanelBottomTexture, (float)PanelX, (float)PanelY + 256.0f, 190.0f, 177.0f, 0.0f, 0.0f, 0.7421875f, 0.69140625f, 1, 1);
        HGDIOBJ font = gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_BOLD));
        m_dwTextColor = 0xFFDCDCDC;
        TextCenter(PanelX, PanelY + 12, PanelWidth, "Mu Helper");
        gFont.SelectFont(gFont.GetTextDC(), font);
        m_dwTextColor = m_Tab == 0 ? 0xFFFFFFFF : 0xFF909090;
        TextCenter(ContentX, PanelY + 32, 60, "Ca\xE7" "ando");
        m_dwTextColor = m_Tab == 1 ? 0xFFFFFFFF : 0xFF909090;
        TextCenter(ContentX + 60, PanelY + 32, 60, "Coletando");
        m_dwTextColor = 0xFFFFFFFF;
        Rect(ContentX + 8 + m_Tab * 60, PanelY + 45, 44, 1, .85f, .65f, .25f, 1);
    }
    if (Hit(ContentX, PanelY + 30, 60, 16)) { m_Tab = 0; m_TextFocus = false; }
    if (Hit(ContentX + 60, PanelY + 30, 60, 16)) { m_Tab = 1; m_DelayFocus = nullptr; }
    if (m_Tab == 0) RunHunting(); else RunPicking();

    if (Button(PanelX + (PanelWidth - 96) / 2, PanelY + PanelHeight - 54, 96, 18, "Resetar Config")) ResetConfig();

    const int closeX = ContentX, closeY = PanelY + PanelHeight - 32;
    if (m_Rendering) {
        const bool hover = Inside(closeX, closeY, 24, 24);
        GL_ResetState();
        glColor4f(1, 1, 1, 1);
        GL_DrawTexture(CloseBackTexture, (float)closeX, (float)closeY, 24.0f, 24.0f, 0.0f, 0.0f, 0.75f, 0.75f, 1, 1);
        GL_DrawTexture(hover ? CloseHoverTexture : CloseTexture, (float)closeX, (float)closeY, 24.0f, 24.0f, 0.0f, 0.0f, 0.75f, 0.75f, 1, 1);
    }
    if (Hit(closeX, closeY, 24, 24)) Toggle();
}

void CMuHelper::RunBar()
{
    const int play = BarX + BarButton + 2;
    if (m_Rendering) {
        Box(BarX, BarY, BarButton, BarButton, true, m_Open);
        static const char* const gear[12] = {
            "....#..#....", ".##.####.##.", ".##########.", "..###..###..",
            "####....####", "###......###", "###......###", "####....####",
            "..###..###..", ".##########.", ".##.####.##.", "....#..#...." };
        for (int row = 0; row < 12; ++row) {
            for (int col = 0; col < 12; ++col) {
                if (gear[row][col] == '#') Rect(BarX + 5 + col, BarY + 5 + row, 1, 1, .9f, .75f, .4f, 1);
            }
        }
        Box(play, BarY, BarButton, BarButton, true, false);
        if (m_Active) {
            Rect(play + 7, BarY + 6, 3, 10, .95f, .8f, .2f, 1);
            Rect(play + 12, BarY + 6, 3, 10, .95f, .8f, .2f, 1);
        } else {
            for (int row = 0; row < 11; ++row) {
                const int len = (row <= 5 ? row : 10 - row) * 2 + 1;
                Rect(play + 6, BarY + 6 + row, len, 1, .2f, .85f, .2f, 1);
            }
        }
    }
    if (Hit(BarX, BarY, BarButton, BarButton)) Toggle();
    if (Hit(play, BarY, BarButton, BarButton)) SendStart(!m_Active);
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
