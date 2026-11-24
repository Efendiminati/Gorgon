#pragma once

#include "Scene.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <string>

namespace Gorgon :: Editor {

    /// A single undoable edit. Commands are immutable once pushed; Undo/Redo are const so the
    /// same command instance can be replayed any number of times.
    class Command {
    public:
        virtual ~Command() = default;

        /// Short human readable name shown in the toolbar ("Move", "Delete", ...).
        virtual const std::string &GetName() const = 0;

        /// Reverts this command on the given scene.
        virtual void Undo(Scene &scene) const = 0;

        /// Reapplies this command on the given scene.
        virtual void Redo(Scene &scene) const = 0;

        /// Absorbs a following command of the same kind so a continuous drag, or a burst of
        /// keystrokes in one widget, collapses into a single undo step. Returns false when the
        /// commands cannot be merged, which keeps the default one-step-per-edit behaviour.
        virtual bool Merge(const Command &following) { return false; }
    };

    /// Records a whole-scene before/after pair.
    ///
    /// Every editor edit path (viewport drag/scale, inspector property edits, entity
    /// add/delete/duplicate/reparent, asset-browser inserts) funnels into this command, which
    /// keeps undo/redo exact for structural changes without every call site having to describe
    /// a diff. Scenes are small, so the copies are cheap.
    class SceneSnapshotCommand : public Command {
    public:
        SceneSnapshotCommand(std::string name, Scene before, Scene after);

        const std::string &GetName() const override { return name_; }
        void Undo(Scene &scene) const override;
        void Redo(Scene &scene) const override;

        /// Keeps this command's `before` and adopts the following command's `after`, so dragging
        /// an entity produces one undo step instead of one per mouse move.
        bool Merge(const Command &following) override;

        const Scene &GetBefore() const { return before_; }
        const Scene &GetAfter() const { return after_; }

    private:
        std::string name_;
        Scene before_;
        Scene after_;
    };

    /// Bounded undo/redo stack.
    class CommandStack {
    public:
        explicit CommandStack(std::size_t limit = 200);

        /// Changes the maximum number of undo steps kept. Trims the oldest entries.
        void SetLimit(std::size_t limit);
        std::size_t GetLimit() const { return limit_; }

        /// Drops all history (e.g. after New/Open).
        void Clear();

        bool CanUndo() const { return !undo_.empty(); }
        bool CanRedo() const { return !redo_.empty(); }

        std::size_t GetUndoCount() const { return undo_.size(); }
        std::size_t GetRedoCount() const { return redo_.size(); }

        /// Name of the command Undo would revert, empty when the stack is empty.
        const std::string &GetUndoName() const;
        /// Name of the command Redo would reapply, empty when the stack is empty.
        const std::string &GetRedoName() const;

        /// Pushes a new command, discarding the redo branch. When `merge` is true and the top
        /// undo command accepts it via Command::Merge, the new command is folded into that step.
        void Push(std::unique_ptr<Command> command, bool merge = false);

        /// Reverts the top undo command on the scene and moves it to the redo stack.
        bool Undo(Scene &scene);

        /// Reapplies the top redo command on the scene and moves it to the undo stack.
        bool Redo(Scene &scene);

    private:
        std::deque<std::unique_ptr<Command>> undo_;
        std::deque<std::unique_ptr<Command>> redo_;
        std::size_t limit_ = 200;
    };

} // namespace Gorgon::Editor
