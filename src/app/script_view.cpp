#include "app/script_view.hpp"
#include <iostream>
#include <stdexcept>

namespace ef {
ScriptView::ScriptView(const Content& content, const Options& options)
    : library_(content),
      runtime_([this, variant = options.variant](const std::string& id) { return library_.compile(id, variant); },
               [this](const std::string& current, const std::string& target) { return library_.resolve(current, target); },
               options.script, {options.variant, options.text_preview}),
      template_(Scene::load(content, content.read_json("scripts/index.json").at("view").get<std::string>())), options_(options) {
    if (const auto* choice = choice_layer(); !choice || choice->height <= 0)
        throw std::runtime_error("Script view requires a script_choice text slot with positive row height");
    if (!options.text_preview) template_.title = "ef · 严格剧本执行（开发布局）";
    std::cout << "script entry=" << options.script << " variant=" << options.variant
              << " mode=" << (options.text_preview ? "text-preview" : "strict") << '\n';
}
const Layer* ScriptView::choice_layer() const {
    for (const auto& layer : template_.layers) if (layer.slot == "script_choice") return &layer;
    return nullptr;
}
void ScriptView::update(double seconds) {
    runtime_.update(seconds);
    while (reported_ < runtime_.diagnostics().size()) std::cerr << "[script] " << runtime_.diagnostics()[reported_++] << '\n';
    if (const auto& message = runtime_.message()) {
        const auto anchor = message->script + ':' + std::to_string(message->source_line);
        if (anchor != last_message_) {
            if (!options_.trace_messages)
                std::cout << "message " << message->script << " L0=" << message->id << " source_line=" << message->source_line << std::endl;
            last_message_ = anchor;
        }
    }
    if (runtime_.state() != last_state_) {
        if (runtime_.state() == ScriptState::choices) std::cout << "choices count=" << runtime_.choices().size() << std::endl;
        if (runtime_.state() == ScriptState::ended) std::cout << "script ended" << std::endl;
        last_state_ = runtime_.state();
    }
}
void ScriptView::choose(std::size_t index) {
    if (runtime_.state() == ScriptState::choices && index < runtime_.choices().size()) runtime_.choose(index);
}
void ScriptView::click(Point point) {
    if (runtime_.state() != ScriptState::choices) { runtime_.advance(); return; }
    const auto* row = choice_layer();
    if (!row || point.x < row->x || point.x >= row->x + row->width || point.y < row->y) return;
    choose(static_cast<std::size_t>((point.y - row->y) / row->height));
}
Scene ScriptView::scene() const {
    Scene result = template_;
    result.layers.clear();
    const auto& message = runtime_.message();
    for (auto layer : template_.layers) {
        if (layer.slot == "script_mode") layer.text = options_.text_preview ? "ef · 剧本文字校阅" : "ef · 严格执行（开发布局）";
        else if (layer.slot == "script_help" && !options_.text_preview)
            layer.text = "点击 / 空格 / 回车推进；数字键或点击选择。\n严格模式遇到未实现命令会停止；此布局仅供开发验证。";
        else if (layer.slot == "script_status") {
            layer.text = std::string(options_.text_preview ? "文字校阅" : "严格执行") + " | variant=" + options_.variant;
            if (message) layer.text += " | " + message->script + " | L0=" + std::to_string(message->id) + " | line=" + std::to_string(message->source_line);
            if (runtime_.state() == ScriptState::ended) layer.text += " | 结束";
            layer.text += "\n未实现诊断种类：" + std::to_string(runtime_.diagnostics().size());
        } else if (layer.slot == "script_name") {
            layer.text = message ? message->name : "";
            if (layer.text.starts_with('#')) layer.text.erase(0, 1); // display only; original marker is preserved in Message
        } else if (layer.slot == "script_text") {
            layer.text = message ? message_display_text(message->text) : "";
            if (message && layer.text.empty()) layer.text = "（空文本消息，点击推进）";
        } else if (layer.slot == "script_choice") {
            for (std::size_t i = 0; i < runtime_.choices().size(); ++i) {
                auto row = layer;
                row.y += static_cast<float>(i) * layer.height;
                row.text = std::to_string(i + 1) + ". " + runtime_.choices()[i].text;
                result.layers.push_back(std::move(row));
            }
            continue;
        }
        result.layers.push_back(std::move(layer));
    }
    return result;
}
}
