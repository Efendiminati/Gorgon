#pragma once

#include "Core/Scene.h"
#include "Viewport.h"

#include <Gorgon/Graphics/Color.h>
#include <Gorgon/UI/Dimension.h>
#include <Gorgon/UI/Widget.h>
#include <Gorgon/Widgets/Button.h>
#include <Gorgon/Widgets/Checkbox.h>
#include <Gorgon/Widgets/ColorPicker.h>
#include <Gorgon/Widgets/Dropdown.h>
#include <Gorgon/Widgets/GeometryBoxes.h>
#include <Gorgon/Widgets/Label.h>
#include <Gorgon/Widgets/Numberbox.h>
#include <Gorgon/Widgets/Panel.h>
#include <Gorgon/Widgets/Textbox.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// Converts the editor's byte color into the widget layer's float color.
    inline Gorgon::Graphics::RGBAf ToColorF(const ColorRGBA &color) {
        return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    }

    /// Converts a widget color back into the editor's byte color.
    inline ColorRGBA FromColorF(const Gorgon::Graphics::RGBAf &color) {
        const auto quantize = [](float value) -> std::uint8_t {
            const int rounded = int(value * 255.0f + 0.5f);
            if(rounded < 0)
                return 0;
            if(rounded > 255)
                return 255;
            return std::uint8_t(rounded);
        };

        return {quantize(color.R), quantize(color.G), quantize(color.B), quantize(color.A)};
    }

    /**
     * Right dock: details / property inspector.
     *
     * Binds to the entity currently selected in the viewport; when nothing is selected it
     * shows the scene settings instead. Edits are written straight back into the Scene so
     * the viewport can redraw from it immediately. Values are only pushed back into a
     * widget that does not hold focus, so a running edit is never clobbered mid-typing.
     */
    class InspectorPanel {
    public:
        InspectorPanel(Scene &scene, EditorViewport &viewport, Gorgon::Widgets::Panel &panel);
        ~InspectorPanel();

        InspectorPanel(const InspectorPanel &) = delete;
        InspectorPanel &operator=(const InspectorPanel &) = delete;

        /// Full rebind: swaps between scene settings and the selected entity, then re-lays out.
        void Rebuild();

        /// Pulls the current Scene values into every widget that is not being edited.
        void Refresh();

        /// Repositions every row from the panel's interior size.
        void Layout();

        /// Fired after any edit that changes the scene.
        std::function<void()> OnSceneModified;

        /// Fired after any edit that changes the entity tree (component add/remove, rename).
        std::function<void()> OnStructureChanged;

        /// Fired when the user asks the Asset Browser to pick a sprite (false) or script (true)
        /// path for the selected entity. EditorApp routes this to AssetBrowserPanel::BeginPicker.
        std::function<void(bool isScript)> OnBrowseRequested;

        /// Fired by the script section's "Edit Script" button with the selected entity's script path
        /// (as stored in the scene, so the host resolves it against the scene folder).
        std::function<void(const std::string &path)> OnEditScriptRequested;

        /// Prefab actions for the selected entity: save it as a .gprefab.json, push the prefab's
        /// current contents into this instance, or reset the instance to the prefab on disk.
        std::function<void(EntityId)> OnSavePrefabRequested;
        std::function<void(EntityId)> OnApplyPrefabRequested;
        std::function<void(EntityId)> OnRevertPrefabRequested;

        /// Applies a picked asset path to the selected entity's sprite or script component,
        /// creating the component when it is missing. Used by the asset-browser picker.
        void SetPickedAsset(const std::string &path, bool isScript);

        /// True when >1 entity is selected and the multi-selection summary is displayed.
        bool IsMultiSelection() const { return viewport_.GetSelectionCount() > 1; }

        /// Current multi-selection summary text (empty unless >1 entity is selected).
        std::string GetMultiSelectionSummary() const { return lblMultiSummary_.Text; }

    private:
        using Widget = Gorgon::UI::Widget;

        struct Row {
            std::vector<Widget *> widgets;
            std::vector<float> weights;
        };

        void build();
        void buildRows();
        void registerEvents();
        void updateVisibility();
        void pushValues();

        Entity *selectedEntity();
        void modified(bool structural = false);

        void addSprite();
        void removeSprite();
        void addCollider();
        void removeCollider();
        void addScript();
        void removeScript();

        /// One of the optional components the expanded inspector can add and remove.
        enum class ComponentKind {
            SpriteAnim,
            Text,
            Audio,
            Camera,
            Tilemap,
            Particles,
            Light
        };

        void registerComponentEvents();
        void addComponent(ComponentKind kind);
        void removeComponent(ComponentKind kind);
        void onColliderTypeChanged(long index);

        Scene &scene_;
        EditorViewport &viewport_;
        Gorgon::Widgets::Panel &panel_;

        // ---- Multi-selection summary (shown when >1 entity is selected) --------
        Gorgon::Widgets::Label     lblMultiSelection_{"Selection", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblMultiSummary_{""};
        Gorgon::Widgets::Label     lblMultiHint_{"Drag to move together; Ctrl+D duplicate, Delete remove."};

        // ---- Scene settings (shown when nothing is selected) -------------------
        Gorgon::Widgets::Label     lblScene_{"Scene", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblSceneName_{"Name"};
        Gorgon::Widgets::Textbox   sceneName_;
        Gorgon::Widgets::Label     lblGridSize_{"Grid Size"};
        Gorgon::Widgets::Integerbox gridSize_;
        Gorgon::Widgets::Label     lblBackground_{"Background"};
        Gorgon::Widgets::ColorPicker backgroundColor_;

        // ---- Entity ------------------------------------------------------------
        Gorgon::Widgets::Label     lblEntity_{"Entity", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblEntityName_{"Name"};
        Gorgon::Widgets::Textbox   entityName_;
        Gorgon::Widgets::Checkbox  chkVisible_{"Visible", true};
        Gorgon::Widgets::Checkbox  chkLocked_{"Locked", false};
        Gorgon::Widgets::Label     lblZOrder_{"Z Order"};
        Gorgon::Widgets::Integerbox zOrder_;

        // ---- Transform ---------------------------------------------------------
        Gorgon::Widgets::Label     lblTransform_{"Transform", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblPosition_{"Position"};
        Gorgon::Widgets::Pointfbox position_;
        Gorgon::Widgets::Label     lblScale_{"Scale"};
        Gorgon::Widgets::Pointfbox scale_;
        Gorgon::Widgets::Label     lblRotation_{"Rotation"};
        Gorgon::Widgets::Numberbox rotation_;

        // ---- Sprite ------------------------------------------------------------
        Gorgon::Widgets::Label     lblSprite_{"Sprite", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblSpriteAsset_{"Asset"};
        Gorgon::Widgets::Textbox   spriteAsset_;
        Gorgon::Widgets::Button    btnPickSprite_{"..."};
        Gorgon::Widgets::Label     lblSpriteTint_{"Tint"};
        Gorgon::Widgets::ColorPicker spriteTint_;
        Gorgon::Widgets::Button    btnAddSprite_{"+ Add Sprite"};
        Gorgon::Widgets::Button    btnRemoveSprite_{"- Remove Sprite"};

        // ---- Collider ----------------------------------------------------------
        Gorgon::Widgets::Label     lblCollider_{"Collider", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblColliderType_{"Type"};
        Gorgon::Widgets::DropdownList<std::string> colliderType_;
        Gorgon::Widgets::Label     lblColliderSize_{"W / H"};
        Gorgon::Widgets::Pointfbox colliderSize_;
        Gorgon::Widgets::Label     lblColliderRadius_{"Radius"};
        Gorgon::Widgets::Numberbox colliderRadius_;
        Gorgon::Widgets::Label     lblColliderOffset_{"Offset"};
        Gorgon::Widgets::Pointfbox colliderOffset_;
        Gorgon::Widgets::Checkbox  chkColliderTrigger_{"Trigger", false};
        Gorgon::Widgets::Checkbox  chkColliderStatic_{"Static", false};
        Gorgon::Widgets::Button    btnAddCollider_{"+ Add Collider"};
        Gorgon::Widgets::Button    btnRemoveCollider_{"- Remove Collider"};

        // ---- Script ------------------------------------------------------------
        Gorgon::Widgets::Label     lblScript_{"Script", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblScriptPath_{"Path"};
        Gorgon::Widgets::Textbox   scriptPath_;
        Gorgon::Widgets::Button    btnPickScript_{"..."};
        Gorgon::Widgets::Button    btnEditScript_{"Edit Script"};
        Gorgon::Widgets::Button    btnAddScript_{"+ Add Script"};
        Gorgon::Widgets::Button    btnRemoveScript_{"- Remove Script"};

        // ---- Prefab ------------------------------------------------------------
        Gorgon::Widgets::Label     lblPrefab_{"Prefab", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblPrefabSource_{"Source"};
        Gorgon::Widgets::Label     lblPrefabPath_{""};
        Gorgon::Widgets::Button    btnApplyPrefab_{"Apply"};
        Gorgon::Widgets::Button    btnRevertPrefab_{"Revert"};
        Gorgon::Widgets::Button    btnSavePrefab_{"Save as Prefab"};


        // ---- Tags (always available on an entity) ------------------------------
        Gorgon::Widgets::Label     lblTags_{"Tags", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Textbox   tags_{""};

        // ---- Sprite animation --------------------------------------------------
        Gorgon::Widgets::Label     lblAnim_{"Sprite Animation", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblAnimAsset_{"Sheet"};
        Gorgon::Widgets::Textbox   animAsset_{""};
        Gorgon::Widgets::Label     lblAnimFps_{"FPS"};
        Gorgon::Widgets::Numberbox animFps_{12.0f};
        Gorgon::Widgets::Button    btnAddAnim_{"+ Add Animation"};
        Gorgon::Widgets::Button    btnRemoveAnim_{"- Remove Animation"};

        // ---- Text --------------------------------------------------------------
        Gorgon::Widgets::Label     lblText_{"Text", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblTextValue_{"Value"};
        Gorgon::Widgets::Textbox   textValue_{""};
        Gorgon::Widgets::Label     lblTextSize_{"Size"};
        Gorgon::Widgets::Numberbox textSize_{16.0f};
        Gorgon::Widgets::Button    btnAddText_{"+ Add Text"};
        Gorgon::Widgets::Button    btnRemoveText_{"- Remove Text"};

        // ---- Audio -------------------------------------------------------------
        Gorgon::Widgets::Label     lblAudio_{"Audio", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblAudioAsset_{"Asset"};
        Gorgon::Widgets::Textbox   audioAsset_{""};
        Gorgon::Widgets::Label     lblAudioVolume_{"Volume"};
        Gorgon::Widgets::Numberbox audioVolume_{1.0f};
        Gorgon::Widgets::Button    btnAddAudio_{"+ Add Audio"};
        Gorgon::Widgets::Button    btnRemoveAudio_{"- Remove Audio"};

        // ---- Camera ------------------------------------------------------------
        Gorgon::Widgets::Label     lblCameraC_{"Camera", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblCameraZoom_{"Zoom"};
        Gorgon::Widgets::Numberbox cameraZoom_{1.0f};
        Gorgon::Widgets::Label     lblCameraFollow_{"Follow Id"};
        Gorgon::Widgets::Integerbox cameraFollow_{0};
        Gorgon::Widgets::Button    btnAddCamera_{"+ Add Camera"};
        Gorgon::Widgets::Button    btnRemoveCamera_{"- Remove Camera"};

        // ---- Tilemap -----------------------------------------------------------
        Gorgon::Widgets::Label     lblTilemap_{"Tilemap", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblTilemapCols_{"Columns"};
        Gorgon::Widgets::Integerbox tilemapCols_{0};
        Gorgon::Widgets::Label     lblTilemapRows_{"Rows"};
        Gorgon::Widgets::Integerbox tilemapRows_{0};
        Gorgon::Widgets::Label     lblTilesetAsset_{"Tileset"};
        Gorgon::Widgets::Textbox   tilesetAsset_{""};
        Gorgon::Widgets::Label     lblTileTool_{"Paint Tool"};
        Gorgon::Widgets::DropdownList<std::string> tileTool_;
        Gorgon::Widgets::Label     lblPaintTile_{"Tile Index"};
        Gorgon::Widgets::Integerbox paintTile_{1};
        Gorgon::Widgets::Button    btnAddTilemap_{"+ Add Tilemap"};
        Gorgon::Widgets::Button    btnRemoveTilemap_{"- Remove Tilemap"};

        // ---- Particles ---------------------------------------------------------
        Gorgon::Widgets::Label     lblParticles_{"Particles", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblParticlesRate_{"Emit / s"};
        Gorgon::Widgets::Numberbox particlesRate_{20.0f};
        Gorgon::Widgets::Label     lblParticlesSize_{"Size"};
        Gorgon::Widgets::Numberbox particlesSize_{4.0f};
        Gorgon::Widgets::Button    btnAddParticles_{"+ Add Particles"};
        Gorgon::Widgets::Button    btnRemoveParticles_{"- Remove Particles"};

        // ---- Light -------------------------------------------------------------
        Gorgon::Widgets::Label     lblLight_{"Light", Gorgon::Widgets::Registry::Label_Subtitle};
        Gorgon::Widgets::Label     lblLightType_{"Type"};
        Gorgon::Widgets::DropdownList<std::string> lightType_;
        Gorgon::Widgets::Label     lblLightColor_{"Colour"};
        Gorgon::Widgets::ColorPicker lightColor_;
        Gorgon::Widgets::Label     lblLightRadius_{"Radius"};
        Gorgon::Widgets::Numberbox lightRadius_{256.0f};
        Gorgon::Widgets::Label     lblLightIntensity_{"Intensity"};
        Gorgon::Widgets::Numberbox lightIntensity_{1.0f};
        Gorgon::Widgets::Label     lblLightCone_{"Cone Angle"};
        Gorgon::Widgets::Numberbox lightCone_{60.0f};
        Gorgon::Widgets::Label     lblLightDirection_{"Direction"};
        Gorgon::Widgets::Numberbox lightDirection_{0.0f};
        Gorgon::Widgets::Checkbox  chkLightShadows_{"Cast Shadows", false};
        Gorgon::Widgets::Button    btnAddLight_{"+ Add Light"};
        Gorgon::Widgets::Button    btnRemoveLight_{"- Remove Light"};

        std::vector<Row> rows_;
        bool updating_ = false;
        Gorgon::Event<Gorgon::UI::Widget>::Token boundsToken_ =
            Gorgon::Event<Gorgon::UI::Widget>::EmptyToken;
    };

} // namespace Gorgon::Editor
