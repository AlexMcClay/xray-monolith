#pragma once
#include "ui/UIWindow.h"
#include "ui/UIStatic.h"
#include "../../Include/xrRender/RenderVisual.h"
#include "../../Include/xrRender/Kinematics.h"

class CGameObject;
class CScriptGameObject;
class script_attachment;

class CUI3dStatic : public CUIStatic
{
    typedef CUIStatic inherited;
public:
    CUI3dStatic();
    virtual ~CUI3dStatic();

    // Called once immediately before the normal HUD UI traversal.
    static void PrepareAtlas();

    void SetRotation(float x, float y, float z) { m_angle.set(x, y, z); }
    void SetScale(float scale) { m_scale = scale; }
    // Fit by the model's bounding sphere instead of its box as rotated: the size no longer changes as the model turns
    // (a view the player orbits). Off by default (icons fit their cells tightly).
    void SetFixedFit(bool fixed) { m_fixedFit = fixed; }
    void SetObject(CScriptGameObject* obj);
    void SetVisual(LPCSTR name);

    // Extra models on the SetVisual model, placed as script attachments are: on `bone` of `parent` (-1 the
    // SetVisual model, else an index AddVisual returned) times `offset`. Drawn and fitted with it; dropped by
    // ClearVisuals, SetVisual and SetObject. Returns the new model's index, or -1 (no SetVisual model, a bad parent).
    int AddVisual(LPCSTR name, int parent, LPCSTR bone, const Fmatrix& offset);
    void ClearVisuals();
    // Shows or hides a bone (and its children) of the SetVisual model (index -1) or of an extra model.
    void SetVisualBoneVisible(int index, LPCSTR bone, bool visible);

    // Where a point in the shown model's space (the object's or the visual's) is drawn, in UI coordinates (as
    // GetAbsoluteRect), for markers on the model. (-1, -1) with nothing shown.
    Fvector2 ProjectPoint(const Fvector& pos);
    // The shown model's box as drawn, in UI coordinates (its 8 corners projected); all -1 with nothing shown.
    Frect GetModelRect();

    virtual void Draw();

protected:
    // The model's transform in item space (the camera's view space, before the camera), fitting `box` (model space;
    // `radius` its bounding sphere's, for SetFixedFit) to target_rect.
    Fmatrix ItemTransform(const Fbox& box, float radius, const Frect& target_rect, float target_width,
        float target_height, float target_aspect);
    // What is shown and the box it fits: the object's model (its box takes in its world attachments), or the
    // SetVisual model with its extra models. False with nothing shown.
    bool FitBox(IRenderVisual*& visual, Fbox& box, float& radius);

    struct SExtraVisual
    {
        IRenderVisual* visual;
        int parent;
        shared_str bone;
        Fmatrix offset;
        Fmatrix local; // in the SetVisual model's space
    };
    xr_vector<SExtraVisual> m_extraVisuals;
    IRenderVisual* ExtraParent(int parent) const;
    void UpdateExtraVisuals();
    Fvector m_angle;
    float dist, m_scale;
    bool m_fixedFit = false;
    bool m_bFirstUpdate;
    void FromScreenToItem(int x_screen, int y_screen, float& x_item, float& y_item);
    void FromTargetToItem(float x, float y, float target_width, float target_height, float target_aspect,
        float& x_item, float& y_item);
    void QueueVisuals(const Frect& target_rect, float target_width, float target_height, float target_aspect);
    bool ValidateCurrentItem();
    Fmatrix DrawVisualIcon(IRenderVisual* visual, const Fbox& box, float radius, const Frect& target_rect,
        float target_width, float target_height, float target_aspect);
    void DrawAttachmentIcon(script_attachment* att, const Fmatrix& matrix, IKinematics* parent);

    CGameObject* m_pCurrentItem;
    u16 m_currentItemId;
    IRenderVisual* m_pFakeVisual;
    CUIStaticItem m_atlasItem;
    Frect m_atlasRect;
    Frect m_atlasContentRect;
    Fvector2 m_atlasDrawMargin;
    u32 m_atlasFrame;
    bool m_atlasShaderCreated;
};
