#include "InspectorPanel.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <cstddef>
#include <string>

namespace Gorgon :: Editor {

    namespace {
        const char *const ColliderTypeNames[3] = {"none", "aabb", "circle"};

        int ColliderTypeToIndex(ColliderType type) {
            switch(type) {
                case ColliderType::AABB:   return 1;
                case ColliderType::Circle: return 2;
                case ColliderType::None:
                default:                   return 0;
            }
        }

        ColliderType ColliderTypeFromIndex(long index) {
            switch(index) {
                case 1:  return ColliderType::AABB;
                case 2:  return ColliderType::Circle;
                default: return ColliderType::None;
            }
        }
    }

    InspectorPanel::InspectorPanel(Scene &scene, EditorViewport &viewport, Gorgon::Widgets::Panel &panel) :
        scene_(scene), viewport_(viewport), panel_(panel)
    {
        build();
        buildRows();
        registerEvents();
        Rebuild();
    }

    InspectorPanel::~InspectorPanel() {
        panel_.BoundsChangedEvent.Unregister(boundsToken_);
    }

    void InspectorPanel::build() {
        panel_.EnableScroll(true, false);

        // ---- multi-selection -----------------------------------------------
        panel_.Add(lblMultiSelection_);
        panel_.Add(lblMultiSummary_);
        panel_.Add(lblMultiHint_);

        // ---- scene ---------------------------------------------------------
        panel_.Add(lblScene_);
        panel_.Add(lblSceneName_);
        panel_.Add(sceneName_);
        panel_.Add(lblGridSize_);
        panel_.Add(gridSize_);
        panel_.Add(lblBackground_);
        panel_.Add(backgroundColor_);

        // ---- entity --------------------------------------------------------
        panel_.Add(lblEntity_);
        panel_.Add(lblEntityName_);
        panel_.Add(entityName_);
        panel_.Add(chkVisible_);
        panel_.Add(chkLocked_);
        panel_.Add(lblZOrder_);
        panel_.Add(zOrder_);

        // ---- transform -----------------------------------------------------
        panel_.Add(lblTransform_);
        panel_.Add(lblPosition_);
        panel_.Add(position_);
        panel_.Add(lblScale_);
        panel_.Add(scale_);
        panel_.Add(lblRotation_);
        panel_.Add(rotation_);

        // ---- sprite --------------------------------------------------------
        panel_.Add(lblSprite_);
        panel_.Add(lblSpriteAsset_);
        panel_.Add(spriteAsset_);
        panel_.Add(btnPickSprite_);
        panel_.Add(lblSpriteTint_);
        panel_.Add(spriteTint_);
        panel_.Add(btnAddSprite_);
        panel_.Add(btnRemoveSprite_);

        // ---- collider ------------------------------------------------------
        panel_.Add(lblCollider_);
        panel_.Add(lblColliderType_);
        panel_.Add(colliderType_);
        panel_.Add(lblColliderSize_);
        panel_.Add(colliderSize_);
        panel_.Add(lblColliderRadius_);
        panel_.Add(colliderRadius_);
        panel_.Add(lblColliderOffset_);
        panel_.Add(colliderOffset_);
        panel_.Add(chkColliderTrigger_);
        panel_.Add(chkColliderStatic_);
        panel_.Add(btnAddCollider_);
        panel_.Add(btnRemoveCollider_);

        // ---- script --------------------------------------------------------
        panel_.Add(lblScript_);
        panel_.Add(lblScriptPath_);
        panel_.Add(scriptPath_);
        panel_.Add(btnPickScript_);
        panel_.Add(btnEditScript_);
        panel_.Add(btnAddScript_);
        panel_.Add(btnRemoveScript_);

        // ---- prefab --------------------------------------------------------
        panel_.Add(lblPrefab_);
        panel_.Add(lblPrefabSource_);
        panel_.Add(lblPrefabPath_);
        panel_.Add(btnApplyPrefab_);
        panel_.Add(btnRevertPrefab_);
        panel_.Add(btnSavePrefab_);

        // ---- tags -----------------------------------------------------------
        panel_.Add(lblTags_);
        panel_.Add(tags_);

        // ---- sprite animation ----------------------------------------------
        panel_.Add(lblAnim_);
        panel_.Add(lblAnimAsset_);
        panel_.Add(animAsset_);
        panel_.Add(lblAnimFps_);
        panel_.Add(animFps_);
        panel_.Add(btnAddAnim_);
        panel_.Add(btnRemoveAnim_);

        // ---- text ----------------------------------------------------------
        panel_.Add(lblText_);
        panel_.Add(lblTextValue_);
        panel_.Add(textValue_);
        panel_.Add(lblTextSize_);
        panel_.Add(textSize_);
        panel_.Add(btnAddText_);
        panel_.Add(btnRemoveText_);

        // ---- audio ---------------------------------------------------------
        panel_.Add(lblAudio_);
        panel_.Add(lblAudioAsset_);
        panel_.Add(audioAsset_);
        panel_.Add(lblAudioVolume_);
        panel_.Add(audioVolume_);
        panel_.Add(btnAddAudio_);
        panel_.Add(btnRemoveAudio_);

        // ---- camera --------------------------------------------------------
        panel_.Add(lblCameraC_);
        panel_.Add(lblCameraZoom_);
        panel_.Add(cameraZoom_);
        panel_.Add(lblCameraFollow_);
        panel_.Add(cameraFollow_);
        panel_.Add(btnAddCamera_);
        panel_.Add(btnRemoveCamera_);

        // ---- tilemap -------------------------------------------------------
        panel_.Add(lblTilemap_);
        panel_.Add(lblTilemapCols_);
        panel_.Add(tilemapCols_);
        panel_.Add(lblTilemapRows_);
        panel_.Add(tilemapRows_);
        panel_.Add(lblTilesetAsset_);
        panel_.Add(tilesetAsset_);
        panel_.Add(lblTileTool_);
        panel_.Add(tileTool_);
        panel_.Add(lblPaintTile_);
        panel_.Add(paintTile_);
        panel_.Add(btnAddTilemap_);
        panel_.Add(btnRemoveTilemap_);

        // ---- particles -----------------------------------------------------
        panel_.Add(lblParticles_);
        panel_.Add(lblParticlesRate_);
        panel_.Add(particlesRate_);
        panel_.Add(lblParticlesSize_);
        panel_.Add(particlesSize_);
        panel_.Add(btnAddParticles_);
        panel_.Add(btnRemoveParticles_);

        // ---- light ---------------------------------------------------------
        panel_.Add(lblLight_);
        panel_.Add(lblLightType_);
        panel_.Add(lightType_);
        panel_.Add(lblLightColor_);
        panel_.Add(lightColor_);
        panel_.Add(lblLightRadius_);
        panel_.Add(lightRadius_);
        panel_.Add(lblLightIntensity_);
        panel_.Add(lightIntensity_);
        panel_.Add(lblLightCone_);
        panel_.Add(lightCone_);
        panel_.Add(lblLightDirection_);
        panel_.Add(lightDirection_);
        panel_.Add(chkLightShadows_);
        panel_.Add(btnAddLight_);
        panel_.Add(btnRemoveLight_);

        // ---- UI element ----------------------------------------------------
        panel_.Add(lblUI_);
        panel_.Add(lblUIKind_);
        panel_.Add(uiKind_);
        panel_.Add(lblUIId_);
        panel_.Add(uiId_);
        panel_.Add(lblUIText_);
        panel_.Add(uiText_);
        panel_.Add(lblUIFontSize_);
        panel_.Add(uiFontSize_);
        panel_.Add(lblUIOffset_);
        panel_.Add(uiOffset_);
        panel_.Add(lblUISize_);
        panel_.Add(uiSize_);
        panel_.Add(lblUIAnchorH_);
        panel_.Add(uiAnchorH_);
        panel_.Add(lblUIAnchorV_);
        panel_.Add(uiAnchorV_);
        panel_.Add(lblUITextColor_);
        panel_.Add(uiTextColor_);
        panel_.Add(lblUIBackground_);
        panel_.Add(uiBackground_);
        panel_.Add(lblUIHover_);
        panel_.Add(uiHover_);
        panel_.Add(lblUIPressed_);
        panel_.Add(uiPressed_);
        panel_.Add(chkUIVisible_);
        panel_.Add(btnAddUI_);
        panel_.Add(btnRemoveUI_);

        // Sprite tint supports alpha so tint can fade an entity out.
        spriteTint_.SetAlpha(true);
        backgroundColor_.SetAlpha(false);

        colliderType_.List.Add(std::string(ColliderTypeNames[0]));
        colliderType_.List.Add(std::string(ColliderTypeNames[1]));
        colliderType_.List.Add(std::string(ColliderTypeNames[2]));
        colliderType_.List.SetSelectedIndex(0);

        tileTool_.List.Add(std::string("Brush"));
        tileTool_.List.Add(std::string("Fill"));
        tileTool_.List.Add(std::string("Eraser"));
        tileTool_.List.SetSelectedIndex(0);

        lightType_.List.Add(std::string("Point"));
        lightType_.List.Add(std::string("Spot"));
        lightType_.List.SetSelectedIndex(0);

        uiKind_.List.Add(std::string("Button"));
        uiKind_.List.Add(std::string("Label"));
        uiKind_.List.Add(std::string("Panel"));
        uiKind_.List.SetSelectedIndex(0);

        uiAnchorH_.List.Add(std::string("Left"));
        uiAnchorH_.List.Add(std::string("Centre"));
        uiAnchorH_.List.Add(std::string("Right"));
        uiAnchorH_.List.SetSelectedIndex(0);

        uiAnchorV_.List.Add(std::string("Top"));
        uiAnchorV_.List.Add(std::string("Middle"));
        uiAnchorV_.List.Add(std::string("Bottom"));
        uiAnchorV_.List.SetSelectedIndex(0);

        // A widget's colours are drawn as they are, so their alphas are authored too (a
        // transparent panel background is a legitimate look).
        uiTextColor_.SetAlpha(true);
        uiBackground_.SetAlpha(true);
        uiHover_.SetAlpha(true);
        uiPressed_.SetAlpha(true);

        // A light's colour is used as-is by the lightmap, so its alpha is irrelevant.
        lightColor_.SetAlpha(false);

        registerComponentEvents();

        boundsToken_ = panel_.BoundsChangedEvent.Register([this]() {
            Layout();
        });
    }

    void InspectorPanel::buildRows() {
        rows_.clear();

        auto sectionRow = [&](Widget &widget) {
            rows_.push_back(Row{{&widget}, {1.0f}});
        };

        auto fieldRow = [&](Widget &label, Widget &field) {
            rows_.push_back(Row{{&label, &field}, {0.42f, 0.58f}});
        };

        auto pairRow = [&](Widget &left, Widget &right) {
            rows_.push_back(Row{{&left, &right}, {0.5f, 0.5f}});
        };

        auto fullRow = [&](Widget &widget) {
            rows_.push_back(Row{{&widget}, {1.0f}});
        };

        // Label + editable path + asset-browser picker button.
        auto pickRow = [&](Widget &label, Widget &field, Widget &button) {
            rows_.push_back(Row{{&label, &field, &button}, {0.34f, 0.46f, 0.20f}});
        };

        sectionRow(lblMultiSelection_);
        fullRow(lblMultiSummary_);
        fullRow(lblMultiHint_);

        sectionRow(lblScene_);
        fieldRow(lblSceneName_, sceneName_);
        fieldRow(lblGridSize_, gridSize_);
        fieldRow(lblBackground_, backgroundColor_);

        sectionRow(lblEntity_);
        fieldRow(lblEntityName_, entityName_);
        pairRow(chkVisible_, chkLocked_);
        fieldRow(lblZOrder_, zOrder_);

        sectionRow(lblTransform_);
        fieldRow(lblPosition_, position_);
        fieldRow(lblScale_, scale_);
        fieldRow(lblRotation_, rotation_);

        sectionRow(lblSprite_);
        pickRow(lblSpriteAsset_, spriteAsset_, btnPickSprite_);
        fieldRow(lblSpriteTint_, spriteTint_);
        fullRow(btnAddSprite_);
        fullRow(btnRemoveSprite_);

        sectionRow(lblCollider_);
        fieldRow(lblColliderType_, colliderType_);
        fieldRow(lblColliderSize_, colliderSize_);
        fieldRow(lblColliderRadius_, colliderRadius_);
        fieldRow(lblColliderOffset_, colliderOffset_);
        pairRow(chkColliderTrigger_, chkColliderStatic_);
        fullRow(btnAddCollider_);
        fullRow(btnRemoveCollider_);

        sectionRow(lblScript_);
        pickRow(lblScriptPath_, scriptPath_, btnPickScript_);
        fullRow(btnEditScript_);
        fullRow(btnAddScript_);
        fullRow(btnRemoveScript_);

        sectionRow(lblPrefab_);
        fieldRow(lblPrefabSource_, lblPrefabPath_);
        pairRow(btnApplyPrefab_, btnRevertPrefab_);
        fullRow(btnSavePrefab_);

        sectionRow(lblTags_);
        fullRow(tags_);

        sectionRow(lblAnim_);
        fieldRow(lblAnimAsset_, animAsset_);
        fieldRow(lblAnimFps_, animFps_);
        pairRow(btnAddAnim_, btnRemoveAnim_);

        sectionRow(lblText_);
        fieldRow(lblTextValue_, textValue_);
        fieldRow(lblTextSize_, textSize_);
        pairRow(btnAddText_, btnRemoveText_);

        sectionRow(lblAudio_);
        fieldRow(lblAudioAsset_, audioAsset_);
        fieldRow(lblAudioVolume_, audioVolume_);
        pairRow(btnAddAudio_, btnRemoveAudio_);

        sectionRow(lblCameraC_);
        fieldRow(lblCameraZoom_, cameraZoom_);
        fieldRow(lblCameraFollow_, cameraFollow_);
        pairRow(btnAddCamera_, btnRemoveCamera_);

        sectionRow(lblTilemap_);
        fieldRow(lblTilemapCols_, tilemapCols_);
        fieldRow(lblTilemapRows_, tilemapRows_);
        fieldRow(lblTilesetAsset_, tilesetAsset_);
        fieldRow(lblTileTool_, tileTool_);
        fieldRow(lblPaintTile_, paintTile_);
        pairRow(btnAddTilemap_, btnRemoveTilemap_);

        sectionRow(lblParticles_);
        fieldRow(lblParticlesRate_, particlesRate_);
        fieldRow(lblParticlesSize_, particlesSize_);
        pairRow(btnAddParticles_, btnRemoveParticles_);

        sectionRow(lblUI_);
        fieldRow(lblUIKind_, uiKind_);
        fieldRow(lblUIId_, uiId_);
        fieldRow(lblUIText_, uiText_);
        fieldRow(lblUIFontSize_, uiFontSize_);
        fieldRow(lblUIOffset_, uiOffset_);
        fieldRow(lblUISize_, uiSize_);
        fieldRow(lblUIAnchorH_, uiAnchorH_);
        fieldRow(lblUIAnchorV_, uiAnchorV_);
        fieldRow(lblUITextColor_, uiTextColor_);
        fieldRow(lblUIBackground_, uiBackground_);
        fieldRow(lblUIHover_, uiHover_);
        fieldRow(lblUIPressed_, uiPressed_);
        sectionRow(chkUIVisible_);
        pairRow(btnAddUI_, btnRemoveUI_);

        sectionRow(lblLight_);
        fieldRow(lblLightType_, lightType_);
        fieldRow(lblLightColor_, lightColor_);
        fieldRow(lblLightRadius_, lightRadius_);
        fieldRow(lblLightIntensity_, lightIntensity_);
        fieldRow(lblLightCone_, lightCone_);
        fieldRow(lblLightDirection_, lightDirection_);
        sectionRow(chkLightShadows_);
        pairRow(btnAddLight_, btnRemoveLight_);
    }

    Entity *InspectorPanel::selectedEntity() {
        if(viewport_.GetSelectionCount() != 1)
            return nullptr;
        return scene_.FindEntity(viewport_.GetSelectedEntity());
    }

    void InspectorPanel::registerEvents() {
        sceneName_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_ || scene_.name == value)
                return;
            scene_.name = value;
            modified(false);
        });

        gridSize_.ChangedEvent.Register([this](const int &value) {
            if(updating_ || value <= 0 || scene_.settings.gridSize == value)
                return;
            scene_.settings.gridSize = value;
            modified(false);
        });

        backgroundColor_.ChangedEvent.Register([this](const Gorgon::Graphics::RGBAf &value) {
            if(updating_)
                return;
            const ColorRGBA color = FromColorF(value);
            if(scene_.settings.backgroundColor == color)
                return;
            scene_.settings.backgroundColor = color;
            modified(false);
        });

        entityName_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || entity->name == value)
                return;
            entity->name = value;
            modified(true);
        });

        chkVisible_.ChangedEvent.Register([this](bool state) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || entity->visible == state)
                return;
            entity->visible = state;
            modified(false);
        });

        chkLocked_.ChangedEvent.Register([this](bool state) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || entity->locked == state)
                return;
            entity->locked = state;
            modified(true);
        });

        zOrder_.ChangedEvent.Register([this](const int &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || entity->zOrder == value)
                return;
            entity->zOrder = value;
            modified(false);
        });

        position_.ChangedEvent.Register([this](const Gorgon::Geometry::Pointf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity)
                return;
            if(entity->transform.x == value.X && entity->transform.y == value.Y)
                return;
            entity->transform.x = value.X;
            entity->transform.y = value.Y;
            modified(false);
        });

        scale_.ChangedEvent.Register([this](const Gorgon::Geometry::Pointf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity)
                return;
            if(entity->transform.scaleX == value.X && entity->transform.scaleY == value.Y)
                return;
            entity->transform.scaleX = value.X;
            entity->transform.scaleY = value.Y;
            modified(false);
        });

        rotation_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || entity->transform.rotation == value)
                return;
            entity->transform.rotation = value;
            modified(false);
        });

        spriteAsset_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->sprite || entity->sprite->asset == value)
                return;
            entity->sprite->asset = value;
            viewport_.ClearSpriteCache();
            modified(false);
        });

        spriteTint_.ChangedEvent.Register([this](const Gorgon::Graphics::RGBAf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->sprite)
                return;
            const ColorRGBA tint = FromColorF(value);
            if(entity->sprite->tint == tint)
                return;
            entity->sprite->tint = tint;
            modified(false);
        });

        colliderType_.ChangedEvent.Register([this](long index) {
            onColliderTypeChanged(index);
        });

        colliderSize_.ChangedEvent.Register([this](const Gorgon::Geometry::Pointf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->collider)
                return;
            if(entity->collider->width == value.X && entity->collider->height == value.Y)
                return;
            entity->collider->width = value.X;
            entity->collider->height = value.Y;
            modified(false);
        });

        colliderRadius_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->collider || entity->collider->radius == value)
                return;
            entity->collider->radius = value;
            modified(false);
        });

        colliderOffset_.ChangedEvent.Register([this](const Gorgon::Geometry::Pointf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->collider)
                return;
            if(entity->collider->offsetX == value.X && entity->collider->offsetY == value.Y)
                return;
            entity->collider->offsetX = value.X;
            entity->collider->offsetY = value.Y;
            modified(false);
        });

        chkColliderTrigger_.ChangedEvent.Register([this](bool state) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->collider || entity->collider->isTrigger == state)
                return;
            entity->collider->isTrigger = state;
            modified(false);
        });

        chkColliderStatic_.ChangedEvent.Register([this](bool state) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->collider || entity->collider->isStatic == state)
                return;
            entity->collider->isStatic = state;
            modified(false);
        });

        scriptPath_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->script || entity->script->path == value)
                return;
            entity->script->path = value;
            modified(false);
        });

        btnAddSprite_.ClickEvent.Register([this]() { addSprite(); });
        btnRemoveSprite_.ClickEvent.Register([this]() { removeSprite(); });
        btnPickSprite_.ClickEvent.Register([this]() {
            if(OnBrowseRequested) OnBrowseRequested(false);
        });
        btnPickScript_.ClickEvent.Register([this]() {
            if(OnBrowseRequested) OnBrowseRequested(true);
        });
        btnEditScript_.ClickEvent.Register([this]() {
            // Hand the path over exactly as the scene stores it; EditorApp resolves it against the
            // scene folder / asset root, which the panel does not know about.
            const Entity *entity = selectedEntity();
            if(entity && entity->script && OnEditScriptRequested)
                OnEditScriptRequested(entity->script->path);
        });
        btnAddCollider_.ClickEvent.Register([this]() { addCollider(); });
        btnRemoveCollider_.ClickEvent.Register([this]() { removeCollider(); });
        btnAddScript_.ClickEvent.Register([this]() { addScript(); });
        btnRemoveScript_.ClickEvent.Register([this]() { removeScript(); });
        btnSavePrefab_.ClickEvent.Register([this]() {
            if(Entity *entity = selectedEntity(); entity && OnSavePrefabRequested)
                OnSavePrefabRequested(entity->id);
        });
        btnApplyPrefab_.ClickEvent.Register([this]() {
            if(Entity *entity = selectedEntity(); entity && !entity->prefabSource.empty() && OnApplyPrefabRequested)
                OnApplyPrefabRequested(entity->id);
        });
        btnRevertPrefab_.ClickEvent.Register([this]() {
            if(Entity *entity = selectedEntity(); entity && !entity->prefabSource.empty() && OnRevertPrefabRequested)
                OnRevertPrefabRequested(entity->id);
        });
    }

    void InspectorPanel::registerComponentEvents() {
        tags_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity)
                return;

            std::vector<std::string> tags;
            std::string current;
            for(char ch : value) {
                if(ch == ',') {
                    if(!current.empty())
                        tags.push_back(current);
                    current.clear();
                }
                else if(ch != ' ' || !current.empty()) {
                    current.push_back(ch);
                }
            }
            if(!current.empty())
                tags.push_back(current);

            if(tags == entity->tags)
                return;
            entity->tags = std::move(tags);
            modified(false);
        });

        animAsset_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->spriteAnim || entity->spriteAnim->asset == value)
                return;
            entity->spriteAnim->asset = value;
            viewport_.ClearSpriteCache();
            modified(false);
        });
        animFps_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->spriteAnim)
                return;
            entity->spriteAnim->fps = value;
            modified(false);
        });

        textValue_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->text || entity->text->text == value)
                return;
            entity->text->text = value;
            modified(false);
        });
        textSize_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->text)
                return;
            entity->text->fontSize = value;
            modified(false);
        });

        audioAsset_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->audio || entity->audio->asset == value)
                return;
            entity->audio->asset = value;
            modified(false);
        });
        audioVolume_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->audio)
                return;
            entity->audio->volume = value;
            modified(false);
        });

        cameraZoom_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->camera)
                return;
            entity->camera->zoom = value;
            modified(false);
        });
        cameraFollow_.ChangedEvent.Register([this](const int &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->camera)
                return;
            entity->camera->followTarget = static_cast<EntityId>(value < 0 ? 0 : value);
            modified(false);
        });

        tilemapCols_.ChangedEvent.Register([this](const int &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->tilemap)
                return;
            entity->tilemap->cols = value;
            entity->tilemap->tiles.resize(
                static_cast<std::size_t>(std::max(0, entity->tilemap->cols)) *
                static_cast<std::size_t>(std::max(0, entity->tilemap->rows)), -1);
            modified(false);
        });
        tilemapRows_.ChangedEvent.Register([this](const int &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->tilemap)
                return;
            entity->tilemap->rows = value;
            entity->tilemap->tiles.resize(
                static_cast<std::size_t>(std::max(0, entity->tilemap->cols)) *
                static_cast<std::size_t>(std::max(0, entity->tilemap->rows)), -1);
            modified(false);
        });

        tilesetAsset_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->tilemap || entity->tilemap->tilesetAsset == value)
                return;
            entity->tilemap->tilesetAsset = value;
            modified(false);
        });

        // The paint tool and the active tile live in the viewport (the palette strip shares
        // them), so these two only mirror the viewport's state — they never edit the scene.
        tileTool_.ChangedEvent.Register([this](long index) {
            if(updating_)
                return;
            TilemapTool tool = TilemapTool::Brush;
            switch(index) {
                case 1: tool = TilemapTool::Fill; break;
                case 2: tool = TilemapTool::Eraser; break;
                default: tool = TilemapTool::Brush; break;
            }
            viewport_.SetTilemapTool(tool);
        });
        paintTile_.ChangedEvent.Register([this](const int &value) {
            if(updating_)
                return;
            viewport_.SetPaintTile(value);
        });

        particlesRate_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->particles)
                return;
            entity->particles->emitRate = value;
            modified(false);
        });
        particlesSize_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->particles)
                return;
            entity->particles->size = value;
            modified(false);
        });

        lightType_.ChangedEvent.Register([this](long index) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->light)
                return;
            entity->light->type = index == 1 ? LightType::Spot : LightType::Point;
            modified(false);
        });
        lightColor_.ChangedEvent.Register([this](const Gorgon::Graphics::RGBAf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->light)
                return;
            ColorRGBA color = FromColorF(value);
            color.a = entity->light->color.a;
            if(color == entity->light->color)
                return;
            entity->light->color = color;
            modified(false);
        });
        lightRadius_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->light)
                return;
            const float radius = std::max(0.0f, value);
            if(radius == entity->light->radius)
                return;
            entity->light->radius = radius;
            modified(false);
        });
        lightIntensity_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->light)
                return;
            const float intensity = std::max(0.0f, value);
            if(intensity == entity->light->intensity)
                return;
            entity->light->intensity = intensity;
            modified(false);
        });
        lightCone_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->light)
                return;
            const float cone = std::clamp(value, 1.0f, 360.0f);
            if(cone == entity->light->coneAngle)
                return;
            entity->light->coneAngle = cone;
            modified(false);
        });
        lightDirection_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->light)
                return;
            if(value == entity->light->direction)
                return;
            entity->light->direction = value;
            modified(false);
        });
        chkLightShadows_.ChangedEvent.Register([this](const bool &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->light || entity->light->castShadows == value)
                return;
            entity->light->castShadows = value;
            modified(false);
        });

        btnAddAnim_.ClickEvent.Register([this]() { addComponent(ComponentKind::SpriteAnim); });
        btnRemoveAnim_.ClickEvent.Register([this]() { removeComponent(ComponentKind::SpriteAnim); });
        btnAddText_.ClickEvent.Register([this]() { addComponent(ComponentKind::Text); });
        btnRemoveText_.ClickEvent.Register([this]() { removeComponent(ComponentKind::Text); });
        btnAddAudio_.ClickEvent.Register([this]() { addComponent(ComponentKind::Audio); });
        btnRemoveAudio_.ClickEvent.Register([this]() { removeComponent(ComponentKind::Audio); });
        btnAddCamera_.ClickEvent.Register([this]() { addComponent(ComponentKind::Camera); });
        btnRemoveCamera_.ClickEvent.Register([this]() { removeComponent(ComponentKind::Camera); });
        btnAddTilemap_.ClickEvent.Register([this]() { addComponent(ComponentKind::Tilemap); });
        btnRemoveTilemap_.ClickEvent.Register([this]() { removeComponent(ComponentKind::Tilemap); });
        btnAddParticles_.ClickEvent.Register([this]() { addComponent(ComponentKind::Particles); });
        btnRemoveParticles_.ClickEvent.Register([this]() { removeComponent(ComponentKind::Particles); });
        btnAddLight_.ClickEvent.Register([this]() { addComponent(ComponentKind::Light); });
        btnRemoveLight_.ClickEvent.Register([this]() { removeComponent(ComponentKind::Light); });

        // ---- UI element ----------------------------------------------------
        uiKind_.ChangedEvent.Register([this](long index) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            const UIElementKind kind = index == 1 ? UIElementKind::Label
                                     : index == 2 ? UIElementKind::Panel
                                                  : UIElementKind::Button;
            if(entity->ui->kind == kind)
                return;
            entity->ui->kind = kind;
            modified(false);
        });
        uiId_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui || entity->ui->id == value)
                return;
            entity->ui->id = value;
            modified(false);
        });
        uiText_.ChangedEvent.Register([this](const std::string &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui || entity->ui->text == value)
                return;
            entity->ui->text = value;
            modified(false);
        });
        uiFontSize_.ChangedEvent.Register([this](const float &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            const float size = std::max(1.0f, value);
            if(size == entity->ui->fontSize)
                return;
            entity->ui->fontSize = size;
            modified(false);
        });
        uiOffset_.ChangedEvent.Register([this](const Gorgon::Geometry::Pointf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            if(entity->ui->x == value.X && entity->ui->y == value.Y)
                return;
            entity->ui->x = value.X;
            entity->ui->y = value.Y;
            modified(false);
        });
        uiSize_.ChangedEvent.Register([this](const Gorgon::Geometry::Pointf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            const float w = std::max(0.0f, value.X);
            const float h = std::max(0.0f, value.Y);
            if(entity->ui->width == w && entity->ui->height == h)
                return;
            entity->ui->width = w;
            entity->ui->height = h;
            modified(false);
        });
        uiAnchorH_.ChangedEvent.Register([this](long index) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            const UIAnchorH anchor = index == 1 ? UIAnchorH::Center
                                   : index == 2 ? UIAnchorH::Right
                                                : UIAnchorH::Left;
            if(entity->ui->anchorH == anchor)
                return;
            entity->ui->anchorH = anchor;
            modified(false);
        });
        uiAnchorV_.ChangedEvent.Register([this](long index) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            const UIAnchorV anchor = index == 1 ? UIAnchorV::Middle
                                   : index == 2 ? UIAnchorV::Bottom
                                                : UIAnchorV::Top;
            if(entity->ui->anchorV == anchor)
                return;
            entity->ui->anchorV = anchor;
            modified(false);
        });
        uiTextColor_.ChangedEvent.Register([this](const Gorgon::Graphics::RGBAf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            const ColorRGBA color = FromColorF(value);
            if(color == entity->ui->textColor)
                return;
            entity->ui->textColor = color;
            modified(false);
        });
        uiBackground_.ChangedEvent.Register([this](const Gorgon::Graphics::RGBAf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            const ColorRGBA color = FromColorF(value);
            if(color == entity->ui->background)
                return;
            entity->ui->background = color;
            modified(false);
        });
        uiHover_.ChangedEvent.Register([this](const Gorgon::Graphics::RGBAf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            const ColorRGBA color = FromColorF(value);
            if(color == entity->ui->hoverBackground)
                return;
            entity->ui->hoverBackground = color;
            modified(false);
        });
        uiPressed_.ChangedEvent.Register([this](const Gorgon::Graphics::RGBAf &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui)
                return;
            const ColorRGBA color = FromColorF(value);
            if(color == entity->ui->pressedBackground)
                return;
            entity->ui->pressedBackground = color;
            modified(false);
        });
        chkUIVisible_.ChangedEvent.Register([this](const bool &value) {
            if(updating_)
                return;
            Entity *entity = selectedEntity();
            if(!entity || !entity->ui || entity->ui->visible == value)
                return;
            entity->ui->visible = value;
            modified(false);
        });
        btnAddUI_.ClickEvent.Register([this]() { addComponent(ComponentKind::UIElement); });
        btnRemoveUI_.ClickEvent.Register([this]() { removeComponent(ComponentKind::UIElement); });
    }

    void InspectorPanel::addComponent(ComponentKind kind) {
        Entity *entity = selectedEntity();
        if(!entity)
            return;

        switch(kind) {
            case ComponentKind::SpriteAnim:
                if(!entity->spriteAnim)
                    entity->spriteAnim = SpriteAnimComponent{};
                break;
            case ComponentKind::Text:
                if(!entity->text)
                    entity->text = TextComponent{};
                break;
            case ComponentKind::Audio:
                if(!entity->audio)
                    entity->audio = AudioComponent{};
                break;
            case ComponentKind::Camera:
                if(!entity->camera)
                    entity->camera = CameraComponent{};
                break;
            case ComponentKind::Tilemap:
                if(!entity->tilemap) {
                    TilemapComponent tilemap;
                    tilemap.cols = 4;
                    tilemap.rows = 4;
                    tilemap.tiles.assign(16, 0);
                    entity->tilemap = tilemap;
                }
                break;
            case ComponentKind::Particles:
                if(!entity->particles)
                    entity->particles = ParticleComponent{};
                break;
            case ComponentKind::Light:
                if(!entity->light)
                    entity->light = Light2DComponent{};
                break;
            case ComponentKind::UIElement:
                if(!entity->ui) {
                    UIElementComponent element;
                    // A fresh element gets a handle derived from the entity name, so it is already
                    // addressable from a script without any further typing.
                    element.id = entity->name.empty() ? std::string("ui") : entity->name;
                    for(auto &c : element.id) {
                        if(std::isspace(static_cast<unsigned char>(c)))
                            c = '_';
                    }
                    entity->ui = element;
                }
                break;
        }

        modified(true);
    }

    void InspectorPanel::removeComponent(ComponentKind kind) {
        Entity *entity = selectedEntity();
        if(!entity)
            return;

        switch(kind) {
            case ComponentKind::SpriteAnim: entity->spriteAnim.reset(); break;
            case ComponentKind::Text:       entity->text.reset();       break;
            case ComponentKind::Audio:      entity->audio.reset();      break;
            case ComponentKind::Camera:     entity->camera.reset();     break;
            case ComponentKind::Tilemap:    entity->tilemap.reset();    break;
            case ComponentKind::Particles:  entity->particles.reset();  break;
            case ComponentKind::Light:      entity->light.reset();      break;
            case ComponentKind::UIElement:  entity->ui.reset();         break;
        }

        modified(true);
    }

    void InspectorPanel::updateVisibility() {
        const bool multiMode = viewport_.GetSelectionCount() > 1;
        const Entity *entity = multiMode ? nullptr : selectedEntity();
        const bool hasEntity = entity != nullptr;

        const bool sceneMode = !multiMode && !hasEntity;

        lblMultiSelection_.SetVisible(multiMode);
        lblMultiSummary_.SetVisible(multiMode);
        lblMultiHint_.SetVisible(multiMode);

        lblScene_.SetVisible(sceneMode);
        lblSceneName_.SetVisible(sceneMode);
        sceneName_.SetVisible(sceneMode);
        lblGridSize_.SetVisible(sceneMode);
        gridSize_.SetVisible(sceneMode);
        lblBackground_.SetVisible(sceneMode);
        backgroundColor_.SetVisible(sceneMode);

        lblEntity_.SetVisible(hasEntity);
        lblEntityName_.SetVisible(hasEntity);
        entityName_.SetVisible(hasEntity);
        chkVisible_.SetVisible(hasEntity);
        chkLocked_.SetVisible(hasEntity);
        lblZOrder_.SetVisible(hasEntity);
        zOrder_.SetVisible(hasEntity);

        lblTransform_.SetVisible(hasEntity);
        lblPosition_.SetVisible(hasEntity);
        position_.SetVisible(hasEntity);
        lblScale_.SetVisible(hasEntity);
        scale_.SetVisible(hasEntity);
        lblRotation_.SetVisible(hasEntity);
        rotation_.SetVisible(hasEntity);

        const bool hasSprite = hasEntity && entity->sprite.has_value();
        lblSprite_.SetVisible(hasEntity);
        lblSpriteAsset_.SetVisible(hasSprite);
        spriteAsset_.SetVisible(hasSprite);
        btnPickSprite_.SetVisible(hasSprite);
        lblSpriteTint_.SetVisible(hasSprite);
        spriteTint_.SetVisible(hasSprite);
        btnAddSprite_.SetVisible(hasEntity && !hasSprite);
        btnRemoveSprite_.SetVisible(hasSprite);

        const bool hasCollider = hasEntity && entity->collider.has_value();
        const bool isCircle = hasCollider && entity->collider->type == ColliderType::Circle;
        const bool isAABB = hasCollider && entity->collider->type == ColliderType::AABB;

        lblCollider_.SetVisible(hasEntity);
        lblColliderType_.SetVisible(hasEntity);
        colliderType_.SetVisible(hasEntity);
        lblColliderSize_.SetVisible(isAABB);
        colliderSize_.SetVisible(isAABB);
        lblColliderRadius_.SetVisible(isCircle);
        colliderRadius_.SetVisible(isCircle);
        lblColliderOffset_.SetVisible(hasCollider);
        colliderOffset_.SetVisible(hasCollider);
        chkColliderTrigger_.SetVisible(hasCollider);
        chkColliderStatic_.SetVisible(hasCollider);
        btnAddCollider_.SetVisible(hasEntity && !hasCollider);
        btnRemoveCollider_.SetVisible(hasCollider);

        const bool hasScript = hasEntity && entity->script.has_value();
        lblScript_.SetVisible(hasEntity);
        lblScriptPath_.SetVisible(hasScript);
        scriptPath_.SetVisible(hasScript);
        btnPickScript_.SetVisible(hasScript);
        btnEditScript_.SetVisible(hasScript);
        btnAddScript_.SetVisible(hasEntity && !hasScript);
        btnRemoveScript_.SetVisible(hasScript);

        const bool hasAnim = hasEntity && entity->spriteAnim.has_value();
        lblTags_.SetVisible(hasEntity);
        tags_.SetVisible(hasEntity);

        lblAnim_.SetVisible(hasEntity);
        lblAnimAsset_.SetVisible(hasAnim);
        animAsset_.SetVisible(hasAnim);
        lblAnimFps_.SetVisible(hasAnim);
        animFps_.SetVisible(hasAnim);
        btnAddAnim_.SetVisible(hasEntity && !hasAnim);
        btnRemoveAnim_.SetVisible(hasAnim);

        const bool hasText = hasEntity && entity->text.has_value();
        lblText_.SetVisible(hasEntity);
        lblTextValue_.SetVisible(hasText);
        textValue_.SetVisible(hasText);
        lblTextSize_.SetVisible(hasText);
        textSize_.SetVisible(hasText);
        btnAddText_.SetVisible(hasEntity && !hasText);
        btnRemoveText_.SetVisible(hasText);

        const bool hasAudio = hasEntity && entity->audio.has_value();
        lblAudio_.SetVisible(hasEntity);
        lblAudioAsset_.SetVisible(hasAudio);
        audioAsset_.SetVisible(hasAudio);
        lblAudioVolume_.SetVisible(hasAudio);
        audioVolume_.SetVisible(hasAudio);
        btnAddAudio_.SetVisible(hasEntity && !hasAudio);
        btnRemoveAudio_.SetVisible(hasAudio);

        const bool hasCamera = hasEntity && entity->camera.has_value();
        lblCameraC_.SetVisible(hasEntity);
        lblCameraZoom_.SetVisible(hasCamera);
        cameraZoom_.SetVisible(hasCamera);
        lblCameraFollow_.SetVisible(hasCamera);
        cameraFollow_.SetVisible(hasCamera);
        btnAddCamera_.SetVisible(hasEntity && !hasCamera);
        btnRemoveCamera_.SetVisible(hasCamera);

        const bool hasTilemap = hasEntity && entity->tilemap.has_value();
        lblTilemap_.SetVisible(hasEntity);
        lblTilemapCols_.SetVisible(hasTilemap);
        tilemapCols_.SetVisible(hasTilemap);
        lblTilemapRows_.SetVisible(hasTilemap);
        tilemapRows_.SetVisible(hasTilemap);
        lblTilesetAsset_.SetVisible(hasTilemap);
        tilesetAsset_.SetVisible(hasTilemap);
        lblTileTool_.SetVisible(hasTilemap);
        tileTool_.SetVisible(hasTilemap);
        lblPaintTile_.SetVisible(hasTilemap);
        paintTile_.SetVisible(hasTilemap);
        btnAddTilemap_.SetVisible(hasEntity && !hasTilemap);
        btnRemoveTilemap_.SetVisible(hasTilemap);

        const bool hasParticles = hasEntity && entity->particles.has_value();
        lblParticles_.SetVisible(hasEntity);
        lblParticlesRate_.SetVisible(hasParticles);
        particlesRate_.SetVisible(hasParticles);
        lblParticlesSize_.SetVisible(hasParticles);
        particlesSize_.SetVisible(hasParticles);
        btnAddParticles_.SetVisible(hasEntity && !hasParticles);
        btnRemoveParticles_.SetVisible(hasParticles);

        const bool hasLight = hasEntity && entity->light.has_value();
        const bool isSpot = hasLight && entity->light->type == LightType::Spot;
        lblLight_.SetVisible(hasEntity);
        lblLightType_.SetVisible(hasLight);
        lightType_.SetVisible(hasLight);
        lblLightColor_.SetVisible(hasLight);
        lightColor_.SetVisible(hasLight);
        lblLightRadius_.SetVisible(hasLight);
        lightRadius_.SetVisible(hasLight);
        lblLightIntensity_.SetVisible(hasLight);
        lightIntensity_.SetVisible(hasLight);
        // The cone fields only mean something for a spot, so a point light hides them.
        lblLightCone_.SetVisible(isSpot);
        lightCone_.SetVisible(isSpot);
        lblLightDirection_.SetVisible(isSpot);
        lightDirection_.SetVisible(isSpot);
        chkLightShadows_.SetVisible(hasLight);
        btnAddLight_.SetVisible(hasEntity && !hasLight);
        btnRemoveLight_.SetVisible(hasLight);

        const bool hasUI = hasEntity && entity->ui.has_value();
        const bool uiHasText = hasUI && entity->ui->kind != UIElementKind::Panel;
        const bool uiIsButton = hasUI && entity->ui->kind == UIElementKind::Button;
        lblUI_.SetVisible(hasEntity);
        lblUIKind_.SetVisible(hasUI);
        uiKind_.SetVisible(hasUI);
        lblUIId_.SetVisible(hasUI);
        uiId_.SetVisible(hasUI);
        lblUIText_.SetVisible(uiHasText);
        uiText_.SetVisible(uiHasText);
        lblUIFontSize_.SetVisible(uiHasText);
        uiFontSize_.SetVisible(uiHasText);
        lblUIOffset_.SetVisible(hasUI);
        uiOffset_.SetVisible(hasUI);
        lblUISize_.SetVisible(hasUI);
        uiSize_.SetVisible(hasUI);
        lblUIAnchorH_.SetVisible(hasUI);
        uiAnchorH_.SetVisible(hasUI);
        lblUIAnchorV_.SetVisible(hasUI);
        uiAnchorV_.SetVisible(hasUI);
        lblUITextColor_.SetVisible(uiHasText);
        uiTextColor_.SetVisible(uiHasText);
        lblUIBackground_.SetVisible(hasUI);
        uiBackground_.SetVisible(hasUI);
        // Only a button changes colour on hover/press, so the other kinds hide those fields.
        lblUIHover_.SetVisible(uiIsButton);
        uiHover_.SetVisible(uiIsButton);
        lblUIPressed_.SetVisible(uiIsButton);
        uiPressed_.SetVisible(uiIsButton);
        chkUIVisible_.SetVisible(hasUI);
        btnAddUI_.SetVisible(hasEntity && !hasUI);
        btnRemoveUI_.SetVisible(hasUI);

        const bool hasPrefab = hasEntity && !entity->prefabSource.empty();
        lblPrefab_.SetVisible(hasEntity);
        lblPrefabSource_.SetVisible(hasPrefab);
        lblPrefabPath_.SetVisible(hasPrefab);
        btnApplyPrefab_.SetVisible(hasPrefab);
        btnRevertPrefab_.SetVisible(hasPrefab);
        btnSavePrefab_.SetVisible(hasEntity);
    }

    void InspectorPanel::pushValues() {
        updating_ = true;

        const std::size_t selCount = viewport_.GetSelectionCount();
        if(selCount > 1) {
            lblMultiSummary_.Text = std::to_string(selCount) + " entities selected";
            updating_ = false;
            return;
        }

        lblMultiSummary_.Text = "";
        const Entity *entity = selectedEntity();

        if(!entity) {
            sceneName_.Set(scene_.name);
            gridSize_.Set(scene_.settings.gridSize);
            backgroundColor_.Set(ToColorF(scene_.settings.backgroundColor));
            updating_ = false;
            return;
        }

        const bool nameLocked = entityName_.IsFocused();

        if(!nameLocked)
            entityName_.Set(entity->name);

        chkVisible_.SetState(entity->visible);
        chkLocked_.SetState(entity->locked);

        if(!zOrder_.IsFocused())
            zOrder_.Set(entity->zOrder);

        if(!position_.IsFocused())
            position_.Set(Gorgon::Geometry::Pointf{entity->transform.x, entity->transform.y});

        if(!scale_.IsFocused())
            scale_.Set(Gorgon::Geometry::Pointf{entity->transform.scaleX, entity->transform.scaleY});

        if(!rotation_.IsFocused())
            rotation_.Set(entity->transform.rotation);

        if(entity->sprite) {
            if(!spriteAsset_.IsFocused())
                spriteAsset_.Set(entity->sprite->asset);
            spriteTint_.Set(ToColorF(entity->sprite->tint));
        }

        if(entity->collider) {
            colliderType_.List.SetSelectedIndex(ColliderTypeToIndex(entity->collider->type));

            if(!colliderSize_.IsFocused())
                colliderSize_.Set(Gorgon::Geometry::Pointf{entity->collider->width, entity->collider->height});

            if(!colliderRadius_.IsFocused())
                colliderRadius_.Set(entity->collider->radius);

            if(!colliderOffset_.IsFocused())
                colliderOffset_.Set(Gorgon::Geometry::Pointf{entity->collider->offsetX, entity->collider->offsetY});

            chkColliderTrigger_.SetState(entity->collider->isTrigger);
            chkColliderStatic_.SetState(entity->collider->isStatic);
        }

        if(entity->script && !scriptPath_.IsFocused())
            scriptPath_.Set(entity->script->path);

        if(!tags_.IsFocused()) {
            std::string joined;
            for(std::size_t i = 0; i < entity->tags.size(); ++i) {
                if(i)
                    joined += ", ";
                joined += entity->tags[i];
            }
            tags_.Set(joined);
        }

        if(entity->spriteAnim.has_value()) {
            if(!animAsset_.IsFocused())
                animAsset_.Set(entity->spriteAnim->asset);
            if(!animFps_.IsFocused())
                animFps_.Set(entity->spriteAnim->fps);
        }
        if(entity->text.has_value()) {
            if(!textValue_.IsFocused())
                textValue_.Set(entity->text->text);
            if(!textSize_.IsFocused())
                textSize_.Set(entity->text->fontSize);
        }
        if(entity->audio.has_value()) {
            if(!audioAsset_.IsFocused())
                audioAsset_.Set(entity->audio->asset);
            if(!audioVolume_.IsFocused())
                audioVolume_.Set(entity->audio->volume);
        }
        if(entity->camera.has_value()) {
            if(!cameraZoom_.IsFocused())
                cameraZoom_.Set(entity->camera->zoom);
            if(!cameraFollow_.IsFocused())
                cameraFollow_.Set(static_cast<int>(entity->camera->followTarget));
        }
        if(entity->tilemap.has_value()) {
            if(!tilemapCols_.IsFocused())
                tilemapCols_.Set(entity->tilemap->cols);
            if(!tilemapRows_.IsFocused())
                tilemapRows_.Set(entity->tilemap->rows);
            if(!tilesetAsset_.IsFocused())
                tilesetAsset_.Set(entity->tilemap->tilesetAsset);
            if(!paintTile_.IsFocused())
                paintTile_.Set(viewport_.GetPaintTile());
            if(!tileTool_.IsFocused()) {
                const auto tool = viewport_.GetTilemapTool();
                const long index = tool == TilemapTool::Fill ? 1
                                 : tool == TilemapTool::Eraser ? 2 : 0;
                if(tileTool_.List.GetSelectedIndex() != index)
                    tileTool_.List.SetSelectedIndex(index);
            }
        }
        if(entity->particles.has_value()) {
            if(!particlesRate_.IsFocused())
                particlesRate_.Set(entity->particles->emitRate);
            if(!particlesSize_.IsFocused())
                particlesSize_.Set(entity->particles->size);
        }
        if(entity->light.has_value()) {
            const auto &light = *entity->light;
            if(!lightType_.IsFocused()) {
                const long index = light.type == LightType::Spot ? 1 : 0;
                if(lightType_.List.GetSelectedIndex() != index)
                    lightType_.List.SetSelectedIndex(index);
            }
            if(!lightColor_.IsFocused())
                lightColor_.Set(ToColorF(light.color));
            if(!lightRadius_.IsFocused())
                lightRadius_.Set(light.radius);
            if(!lightIntensity_.IsFocused())
                lightIntensity_.Set(light.intensity);
            if(!lightCone_.IsFocused())
                lightCone_.Set(light.coneAngle);
            if(!lightDirection_.IsFocused())
                lightDirection_.Set(light.direction);
            chkLightShadows_.SetState(light.castShadows);
        }
        if(entity->ui.has_value()) {
            const auto &ui = *entity->ui;
            if(!uiKind_.IsFocused()) {
                const long index = ui.kind == UIElementKind::Label ? 1
                                 : ui.kind == UIElementKind::Panel ? 2 : 0;
                if(uiKind_.List.GetSelectedIndex() != index)
                    uiKind_.List.SetSelectedIndex(index);
            }
            if(!uiId_.IsFocused())
                uiId_.Set(ui.id);
            if(!uiText_.IsFocused())
                uiText_.Set(ui.text);
            if(!uiFontSize_.IsFocused())
                uiFontSize_.Set(ui.fontSize);
            if(!uiOffset_.IsFocused())
                uiOffset_.Set(Gorgon::Geometry::Pointf{ui.x, ui.y});
            if(!uiSize_.IsFocused())
                uiSize_.Set(Gorgon::Geometry::Pointf{ui.width, ui.height});
            if(!uiAnchorH_.IsFocused()) {
                const long index = ui.anchorH == UIAnchorH::Center ? 1
                                 : ui.anchorH == UIAnchorH::Right ? 2 : 0;
                if(uiAnchorH_.List.GetSelectedIndex() != index)
                    uiAnchorH_.List.SetSelectedIndex(index);
            }
            if(!uiAnchorV_.IsFocused()) {
                const long index = ui.anchorV == UIAnchorV::Middle ? 1
                                 : ui.anchorV == UIAnchorV::Bottom ? 2 : 0;
                if(uiAnchorV_.List.GetSelectedIndex() != index)
                    uiAnchorV_.List.SetSelectedIndex(index);
            }
            uiTextColor_.Set(ToColorF(ui.textColor));
            uiBackground_.Set(ToColorF(ui.background));
            uiHover_.Set(ToColorF(ui.hoverBackground));
            uiPressed_.Set(ToColorF(ui.pressedBackground));
            chkUIVisible_.SetState(ui.visible);
        }

        lblPrefabPath_.Text = entity->prefabSource;

        updating_ = false;
    }

    void InspectorPanel::Rebuild() {
        updateVisibility();
        pushValues();
        Layout();
    }

    void InspectorPanel::Refresh() {
        if(updating_)
            return;

        updateVisibility();
        pushValues();
    }

    void InspectorPanel::Layout() {
        using Gorgon::UI::Pixels;

        const auto interior = panel_.GetInteriorSize();
        if(interior.Width <= 0 || interior.Height <= 0)
            return;

        const int pad = std::max(4, panel_.GetSpacing());
        int unit = panel_.GetUnitSize();
        if(unit < 18)
            unit = 18;

        const int width = interior.Width - pad * 2;
        if(width < 40)
            return;

        const int gap = 3;
        int y = pad;

        for(const Row &row : rows_) {
            std::vector<Widget *> visible;
            std::vector<float> weights;
            for(std::size_t i = 0; i < row.widgets.size(); ++i) {
                if(row.widgets[i]->IsVisible()) {
                    visible.push_back(row.widgets[i]);
                    weights.push_back(i < row.weights.size() ? row.weights[i] : 1.0f);
                }
            }

            if(visible.empty())
                continue;

            float weightSum = 0.0f;
            for(float weight : weights)
                weightSum += weight;
            if(weightSum <= 0.0f)
                weightSum = 1.0f;

            const int gaps = int(visible.size()) - 1;
            const int usable = std::max(20, width - gap * gaps);
            int x = pad;
            int used = 0;

            for(std::size_t i = 0; i < visible.size(); ++i) {
                int w;
                if(i + 1 == visible.size()) {
                    w = usable - used;
                }
                else {
                    w = std::max(18, int(usable * (weights[i] / weightSum)));
                }

                visible[i]->Move(Pixels(x, y));
                visible[i]->SetWidth(Pixels(w));
                visible[i]->SetHeight(Pixels(unit));

                x += w + gap;
                used += w;
            }

            y += unit + gap;
        }
    }

    void InspectorPanel::modified(bool structural) {
        if(OnSceneModified)
            OnSceneModified();

        if(structural) {
            updateVisibility();
            pushValues();
            Layout();

            if(OnStructureChanged)
                OnStructureChanged();
        }
    }

    void InspectorPanel::addSprite() {
        Entity *entity = selectedEntity();
        if(!entity || entity->sprite)
            return;

        entity->sprite = SpriteComponent{};
        modified(true);
    }

    void InspectorPanel::removeSprite() {
        Entity *entity = selectedEntity();
        if(!entity || !entity->sprite)
            return;

        entity->sprite.reset();
        modified(true);
    }

    void InspectorPanel::addCollider() {
        Entity *entity = selectedEntity();
        if(!entity || entity->collider)
            return;

        entity->collider = ColliderComponent{};
        entity->collider->type = ColliderType::AABB;
        modified(true);
    }

    void InspectorPanel::removeCollider() {
        Entity *entity = selectedEntity();
        if(!entity || !entity->collider)
            return;

        entity->collider.reset();
        modified(true);
    }

    void InspectorPanel::addScript() {
        Entity *entity = selectedEntity();
        if(!entity || entity->script)
            return;

        entity->script = ScriptComponent{};
        modified(true);
    }

    void InspectorPanel::removeScript() {
        Entity *entity = selectedEntity();
        if(!entity || !entity->script)
            return;

        entity->script.reset();
        modified(true);
    }

    void InspectorPanel::SetPickedAsset(const std::string &path, bool isScript) {
        Entity *entity = selectedEntity();
        if(!entity || path.empty())
            return;

        bool structural = false;
        if(isScript) {
            if(!entity->script) {
                entity->script = ScriptComponent{};
                structural = true;
            }
            if(entity->script->path == path && !structural)
                return;
            entity->script->path = path;
        } else {
            if(!entity->sprite) {
                entity->sprite = SpriteComponent{};
                structural = true;
            }
            if(entity->sprite->asset == path && !structural)
                return;
            entity->sprite->asset = path;
            viewport_.ClearSpriteCache();
        }

        modified(structural);
        if(!structural)
            Refresh();
    }

    void InspectorPanel::onColliderTypeChanged(long index) {
        if(updating_)
            return;

        Entity *entity = selectedEntity();
        if(!entity || !entity->collider)
            return;

        const ColliderType type = ColliderTypeFromIndex(index);
        if(entity->collider->type == type)
            return;

        entity->collider->type = type;
        modified(true);
    }

} // namespace Gorgon::Editor
