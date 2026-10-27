#pragma once
#include "scene/presentation.hpp"
#include "scene/message_ui.hpp"
namespace ef {
// Version 1 checkpoints accept settled, manually controlled dialogue or choices.
nlohmann::json capture_checkpoint(const Content&,const ScriptRuntime&,const Presentation&,const MessageUI* ui=nullptr);
// Restore only into a newly constructed, unattached candidate. The application commits
// that candidate after all validation succeeds, so failed loads never mutate a live session.
void restore_checkpoint(const Content&,const nlohmann::json&,ScriptRuntime&,Presentation&,MessageUI* ui=nullptr);
nlohmann::json read_checkpoint(const std::filesystem::path&);
void write_checkpoint(const std::filesystem::path&,const nlohmann::json&);
std::filesystem::path default_checkpoint_path();
}
