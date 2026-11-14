// Conker's settings hierarchy. Saved keys and values remain in the shared runtime.
enum ConkerSection { CRoot, CGraphics, CDisplay, CStereo, CEffects, CAppearance,
    CControls, CPresets, CSticks, CCamera, CAiming, CGyro, CTouch, CButtons,
    CAudio, COutput,
    CAchievements,
    CMisc, CExtras, CDiagnostics, CSaveStates, CSectionCount };
struct ConkerGroup { int parent; const char* title; };
constexpr ConkerGroup kConkerGroups[CSectionCount]={
    {-1,"Categories"},{CRoot,"Graphics"},{CGraphics,"Display"},{CGraphics,"Stereoscopic 3D"},{CGraphics,"Image effects"},{CGraphics,"Appearance"},
    {CRoot,"Controls"},{CControls,"Presets"},{CControls,"Sticks"},{CControls,"Camera"},{CControls,"Aiming"},{CControls,"Gyro"},{CControls,"Touchscreen"},{CControls,"Button mapping"},
    {CRoot,"Audio"},{CAudio,"Output"},
    {CRoot,"Achievements"},
    {CRoot,"Misc"},{CMisc,"Extras"},{CExtras,"Diagnostics"},{CMisc,"Save states"}};
int conker_section_for(const char* key,int page) {
    if(!strncmp(key,"misc_state_",11))return CSaveStates;
    if(!strncmp(key,"ach_",4))return CAchievements;
    if(!strncmp(key,"misc_",5))return CExtras;
    if(!strcmp(key,"control_preset"))return CPresets;
    if(!strncmp(key,"bottom_",7))return CAppearance;
    if(!strncmp(key,"gyro_",5) || !strncmp(key,"look_gyro_",10))return CGyro;
    if(!strncmp(key,"aim_",4) || !strncmp(key,"look_stick_",11))return CAiming;
    if(!strncmp(key,"camera_",7))return CCamera;
    if(!strcmp(key,"model_highest") || !strcmp(key,"cutscene_4x3"))return CDisplay;
    if(page==recomp3ds::Page3D)return CStereo;
    if(!strcmp(key,"frame_cap") || !strcmp(key,"supersampling") || !strncmp(key,"brightness_",11))return CDisplay;
    if(!strcmp(key,"image_filter"))return CEffects;
    if(!strncmp(key,"audio_",6))return COutput;
    if(!strcmp(key,"touch_stick"))return CTouch;
    if(!strcmp(key,"free_camera") || !strcmp(key,"face_camera") || !strncmp(key,"cstick_",7)) {
        if(!strcmp(key,"cstick_deadzone"))return CSticks;
        return CCamera;
    }
    if(!strcmp(key,"stick_deadzone"))return CSticks;
    if(!strncmp(key,"button_",7) || !strcmp(key,"reset_controls"))return CButtons;
    if(!strcmp(key,"options_button") || !strcmp(key,"hud_on_bottom"))return CAppearance;
    return CDiagnostics;
}
