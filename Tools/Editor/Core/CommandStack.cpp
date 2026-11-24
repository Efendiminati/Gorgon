#include "CommandStack.h"

#include <utility>

namespace Gorgon :: Editor {

    namespace {
        const std::string &EmptyName() {
            static const std::string empty;
            return empty;
        }
    } // namespace

    SceneSnapshotCommand::SceneSnapshotCommand(std::string name, Scene before, Scene after) :
        name_(std::move(name)), before_(std::move(before)), after_(std::move(after))
    { }

    void SceneSnapshotCommand::Undo(Scene &scene) const {
        scene = before_;
    }

    void SceneSnapshotCommand::Redo(Scene &scene) const {
        scene = after_;
    }

    bool SceneSnapshotCommand::Merge(const Command &following) {
        const auto *next = dynamic_cast<const SceneSnapshotCommand *>(&following);
        if(!next || next->name_ != name_)
            return false;

        after_ = next->after_;
        return true;
    }

    CommandStack::CommandStack(std::size_t limit) : limit_(limit ? limit : 1) { }

    void CommandStack::SetLimit(std::size_t limit) {
        limit_ = limit ? limit : 1;
        while(undo_.size() > limit_)
            undo_.pop_front();
        while(redo_.size() > limit_)
            redo_.pop_front();
    }

    void CommandStack::Clear() {
        undo_.clear();
        redo_.clear();
    }

    const std::string &CommandStack::GetUndoName() const {
        return undo_.empty() ? EmptyName() : undo_.back()->GetName();
    }

    const std::string &CommandStack::GetRedoName() const {
        return redo_.empty() ? EmptyName() : redo_.back()->GetName();
    }

    void CommandStack::Push(std::unique_ptr<Command> command, bool merge) {
        if(!command)
            return;

        redo_.clear();

        if(merge && !undo_.empty() && undo_.back()->Merge(*command))
            return;

        undo_.push_back(std::move(command));
        while(undo_.size() > limit_)
            undo_.pop_front();
    }

    bool CommandStack::Undo(Scene &scene) {
        if(undo_.empty())
            return false;

        auto command = std::move(undo_.back());
        undo_.pop_back();
        command->Undo(scene);
        redo_.push_back(std::move(command));
        return true;
    }

    bool CommandStack::Redo(Scene &scene) {
        if(redo_.empty())
            return false;

        auto command = std::move(redo_.back());
        redo_.pop_back();
        command->Redo(scene);
        undo_.push_back(std::move(command));
        return true;
    }

} // namespace Gorgon::Editor
