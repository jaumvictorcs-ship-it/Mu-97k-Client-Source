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
    void UpdateMouse();
    void Render();
    // Retorna true enquanto o campo de texto estiver em foco: os atalhos ficam bloqueados.
    bool UpdateTextInput();

private:
    void ResetConfig();
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
    bool Button(int x, int y, int w, int h, const char* label, bool active = false);
    void Check(int x, int y, const char* label, BYTE* flag);
    void Stepper(int x, int y, const char* label, BYTE* value);
    void DelayStepper(int x, int y, WORD* value);
    void SkillSlot(int x, int y, int w, BYTE* skill);

    MUHELPER_CONFIG m_Config = {};
    BYTE* m_PopupTarget = nullptr;
    int m_PopupX = 0;
    int m_PopupY = 0;
    int m_Tab = 0;
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
