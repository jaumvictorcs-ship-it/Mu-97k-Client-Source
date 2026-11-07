#pragma once

#define MUHELPER_EXTRA_COUNT 12
#define MUHELPER_EXTRA_SIZE 16

#pragma pack(push, 1)
// Mesmo layout de HELPER_CONFIG em GameServer/HelperConfig.h (servidor).
struct MUHELPER_CONFIG {
    BYTE HuntingRange;
    BYTE PickRange;
    BYTE ReturnToOriginalPos;
    BYTE BasicSkillID;
    BYTE Skill1ID;
    WORD Skill1Delay;
    BYTE Skill1Enabled;
    BYTE Skill2ID;
    WORD Skill2Delay;
    BYTE Skill2Enabled;
    BYTE AutoBuffEnabled;
    BYTE BuffSkillID[3];
    BYTE PickAll;
    BYTE PickSelected;
    BYTE PickJewels;
    BYTE PickExcellent;
    BYTE PickZen;
    BYTE PickExtra;
    char ExtraItems[MUHELPER_EXTRA_COUNT][MUHELPER_EXTRA_SIZE];
};
#pragma pack(pop)

static_assert(sizeof(MUHELPER_CONFIG) == 214, "MUHELPER_CONFIG deve casar com o servidor");

class CMuHelper {
public:
    void Clear();
    void Toggle();
    void ReceiveConfig(const BYTE* packet, int size);
    void ReceiveState(const BYTE* packet, int size);
    void ReceiveMove(const BYTE* packet, int size);
    void ReceiveAttack(const BYTE* packet, int size);
    void UpdateAttack();
    bool IsAttacking() const { return m_Attacking; }
    void RenderWorld();
    void UpdateMouse();
    void Render();
    // Retorna true enquanto o campo de texto estiver em foco: os atalhos ficam bloqueados.
    bool UpdateTextInput();

private:
    void ResetConfig();
    void ReleaseAttack();
    void SendConfig();
    void SendStart(bool start);
    void MarkDirty();
    void AddExtra();
    void RemoveExtra();
    void Run();
    void RunBar();
    void RunPanel();
    void RunHunting();
    void RunPicking();
    void RunPopup();
    bool Hit(int x, int y, int w, int h);
    bool Button(int x, int y, int w, int h, const char* label, bool active = false);
    bool Arrow(int x, int y, bool up);
    void Check(int x, int y, const char* label, BYTE* flag);
    void Range(int y, BYTE* value);
    void Delay(int x, int y, BYTE* enabled, WORD* value);
    void SkillSlot(int x, int y, BYTE* skill);
    void Section(int y, int h);

    MUHELPER_CONFIG m_Config = {};
    BYTE* m_PopupTarget = nullptr;
    WORD* m_DelayFocus = nullptr;
    int m_PopupX = 0;
    int m_PopupY = 0;
    int m_Tab = 0;
    int m_AnchorX = 0;
    int m_AnchorY = 0;
    WORD m_AttackKey = 0xFFFF;
    BYTE m_AttackSkill = 0;
    char m_SavedAutoAttack = 1;
    bool m_Attacking = false;
    int m_SelectedExtra = -1;
    char m_Text[MUHELPER_EXTRA_SIZE] = {};
    DWORD m_DirtyTick = 0;
    bool m_Open = false;
    bool m_Active = false;
    bool m_Dirty = false;
    bool m_TextFocus = false;
    bool m_Rendering = false;
    bool m_Click = false;
};

extern CMuHelper gMuHelper;
