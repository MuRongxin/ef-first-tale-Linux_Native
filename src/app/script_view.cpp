#include "app/script_view.hpp"
#include <iostream>
#include <stdexcept>

namespace ef {
ScriptView::ScriptView(const Content& content, const Options& options)
    : presentation_(content), library_(content),
      runtime_([this, variant = options.variant](const std::string& id) { return library_.compile(id, variant); },
               [this](const std::string& current, const std::string& target) { return library_.resolve(current, target); },
               options.script, {options.variant, options.text_preview, static_cast<std::size_t>(options.start_line), static_cast<double>(options.text_speed)}),
      template_(Scene::load(content, content.read_json(content.script_index()).at("view").get<std::string>())), options_(options) {
    if (!options.text_preview) runtime_.set_host({
        [this](const Instruction& i) { return presentation_.command(i); },
        [this](const Message& m) { presentation_.message(m); },
        [this](double seconds) { presentation_.update(seconds); },
        [this] { return presentation_.busy(); },
        [this] { return presentation_.mixer().playing(AudioChannel::voice); }});
    if (options.start_line) std::cerr << "[script] Development start line: preceding scene/variables are not restored\n";
    if (const auto* choice = choice_layer(); !choice || choice->height <= 0)
        throw std::runtime_error("Script view requires a script_choice text slot with positive row height");
    if (!options.text_preview) {
        template_.background = {0,0,0,1};
        for (auto& layer : template_.layers) {
            if (layer.kind == LayerKind::rectangle) { layer.y = 420; layer.height = 170; layer.color = {0.02f,0.025f,0.04f,0.8f}; }
            if (layer.slot == "script_name") layer.y = 430;
            if (layer.slot == "script_text") { layer.y = 465; layer.font_size = 22; }
            if (layer.slot == "script_choice") { layer.y = 270; layer.height = 34; }
            if (layer.slot == "script_help") { layer.y = 570; layer.font_size = 12; }
        }
    }
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
        const auto anchor = runtime_.message_serial();
        if (anchor != last_message_) {
            if (!options_.trace_messages)
                std::cout << "message " << message->script << " L0=" << message->id << " source_line=" << message->source_line << std::endl;
            last_message_ = anchor;
        }
        if (!options_.trace_messages && runtime_.message_complete() && anchor != last_ready_) {
            std::cout << "message ready " << message->script << " L0=" << message->id << std::endl;
            last_ready_ = anchor;
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
        if (!options_.text_preview && !presentation_.panel_visible() &&
            (layer.kind == LayerKind::rectangle || layer.slot == "script_name" || layer.slot == "script_text")) continue;
        if (layer.slot == "script_mode") layer.text = options_.text_preview ? "ef · 剧本文字校阅" : "ef · 严格执行（开发布局）";
        else if (layer.slot == "script_help" && !options_.text_preview)
            layer.text = "点击 / 空格 / 回车推进；数字键选择。开发叠层；遇到未实现演出即停止。";
        else if (layer.slot == "script_status") {
            layer.text = std::string(options_.text_preview ? "文字校阅" : "严格执行") + " | variant=" + options_.variant;
            if (message) layer.text += " | " + message->script + " | L0=" + std::to_string(message->id) + " | line=" + std::to_string(message->source_line);
            if (runtime_.state() == ScriptState::ended) layer.text += " | 结束";
            layer.text += "\n未实现诊断种类：" + std::to_string(runtime_.diagnostics().size());
        } else if (layer.slot == "script_name") {
            layer.text = message ? message->name : "";
            if (layer.text.starts_with('#')) layer.text.erase(0, 1); // display only; original marker is preserved in Message
        } else if (layer.slot == "script_text") {
            layer.text = message ? runtime_.displayed_text() : "";
            if (message && layer.text.empty() && runtime_.message_complete() && !runtime_.message_automatic() && runtime_.controls_enabled()) layer.text = "（空文本消息，点击推进）";
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
