#include "InspectorPanel.h"

#include <algorithm>
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

    void InspectorPanel::build() {
        panel_.EnableScroll(true, false);

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
        panel_.Add(btnAddCollider_);
        panel_.Add(btnRemoveCollider_);

        // ---- script --------------------------------------------------------
        panel_.Add(lblScript_);
        panel_.Add(lblScriptPath_);
        panel_.Add(scriptPath_);
        panel_.Add(btnAddScript_);
        panel_.Add(btnRemoveScript_);

        // Sprite tint supports alpha so tint can fade an entity out.
        spriteTint_.SetAlpha(true);
        backgroundColor_.SetAlpha(false);

        colliderType_.List.Add(std::string(ColliderTypeNames[0]));
        colliderType_.List.Add(std::string(ColliderTypeNames[1]));
        colliderType_.List.Add(std::string(ColliderTypeNames[2]));
        colliderType_.List.SetSelectedIndex(0);

        panel_.BoundsChangedEvent.Register([this]() {
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
        fieldRow(lblSpriteAsset_, spriteAsset_);
        fieldRow(lblSpriteTint_, spriteTint_);
        fullRow(btnAddSprite_);
        fullRow(btnRemoveSprite_);

        sectionRow(lblCollider_);
        fieldRow(lblColliderType_, colliderType_);
        fieldRow(lblColliderSize_, colliderSize_);
        fieldRow(lblColliderRadius_, colliderRadius_);
        fieldRow(lblColliderOffset_, colliderOffset_);
        fullRow(btnAddCollider_);
        fullRow(btnRemoveCollider_);

        sectionRow(lblScript_);
        fieldRow(lblScriptPath_, scriptPath_);
        fullRow(btnAddScript_);
        fullRow(btnRemoveScript_);
    }

    Entity *InspectorPanel::selectedEntity() {
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
        btnAddCollider_.ClickEvent.Register([this]() { addCollider(); });
        btnRemoveCollider_.ClickEvent.Register([this]() { removeCollider(); });
        btnAddScript_.ClickEvent.Register([this]() { addScript(); });
        btnRemoveScript_.ClickEvent.Register([this]() { removeScript(); });
    }

    void InspectorPanel::updateVisibility() {
        const Entity *entity = selectedEntity();
        const bool hasEntity = entity != nullptr;

        const bool sceneMode = !hasEntity;

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
        btnAddCollider_.SetVisible(hasEntity && !hasCollider);
        btnRemoveCollider_.SetVisible(hasCollider);

        const bool hasScript = hasEntity && entity->script.has_value();
        lblScript_.SetVisible(hasEntity);
        lblScriptPath_.SetVisible(hasScript);
        scriptPath_.SetVisible(hasScript);
        btnAddScript_.SetVisible(hasEntity && !hasScript);
        btnRemoveScript_.SetVisible(hasScript);
    }

    void InspectorPanel::pushValues() {
        updating_ = true;

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
        }

        if(entity->script && !scriptPath_.IsFocused())
            scriptPath_.Set(entity->script->path);

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
