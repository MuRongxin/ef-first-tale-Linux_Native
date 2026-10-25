#include "scene/message_ui.hpp"
#include "core/utf8.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace ef {
namespace {
float metric(const nlohmann::json& json,const char* name,float low,float high) {
    const float value=json.at(name).get<float>();
    if (!std::isfinite(value) || value<low || value>high) throw std::runtime_error(std::string("Invalid message UI metric: ")+name);
    return value;
}
Layer text_layer(const nlohmann::json& rect,const nlohmann::json& style) {
    const auto bounds=rect.get<std::array<float,4>>();
    if (!std::all_of(bounds.begin(),bounds.end(),[](float n) { return std::isfinite(n); }) ||
        bounds[0]<0 || bounds[1]<0 || bounds[2]<=0 || bounds[3]<=0 || bounds[0]+bounds[2]>800 || bounds[1]+bounds[3]>600)
        throw std::runtime_error("Message text region outside 800x600 canvas");
    Layer layer; layer.kind=LayerKind::text;
    layer.x=bounds[0]; layer.y=bounds[1]; layer.width=bounds[2]; layer.height=bounds[3];
    layer.font_size=metric(style,"font_size",1,256);
    layer.line_height=metric(style,"line_height",1,600);
    layer.letter_spacing=metric(style,"letter_spacing",0,64);
    layer.color=style.at("color").get<Color>();
    if (!std::all_of(layer.color.begin(),layer.color.end(),[](float n) { return std::isfinite(n) && n>=0 && n<=1; }))
        throw std::runtime_error("Invalid message text color");
    layer.kinsoku=true;
    return layer;
}
}
MessageUI::MessageUI(const Content& content,const std::string& path) {
    const auto doc=content.read_json(path);
    if (doc.at("version")!=1 || doc.at("design_size")!=nlohmann::json{800,600}) throw std::runtime_error("Unsupported message UI contract");
    const auto& marker=doc.at("advance");
    advance_asset_=marker.at("asset").get<std::string>();
    frame_size_=marker.at("frame_size").get<int>(); frame_ms_=marker.at("frame_ms").get<int>();
    const auto& atlas=content.asset(advance_asset_);
    if (frame_size_<=0 || frame_ms_<=0 || frame_ms_>10000 || atlas.height!=frame_size_ || atlas.width%frame_size_)
        throw std::runtime_error("Invalid advance sprite strip");
    frames_=atlas.width/frame_size_;
    if (frames_<1 || frames_>256) throw std::runtime_error("Invalid advance frame count");
    if (!doc.at("modes").is_object() || doc.at("modes").empty()) throw std::runtime_error("Message UI requires modes");
    for (const auto& [key,value] : doc.at("modes").items()) {
        int id{}; const auto parsed=std::from_chars(key.data(),key.data()+key.size(),id);
        if (parsed.ec!=std::errc{} || parsed.ptr!=key.data()+key.size() || id<1 || id>5) throw std::runtime_error("Invalid message UI mode");
        Mode mode; mode.name=text_layer(value.at("name"),doc); mode.body=text_layer(value.at("body"),doc);
        mode.name.slot="message_name"; mode.body.slot="message_body";
        if (value.contains("upward_room")) mode.body.upward_room=metric(value,"upward_room",0,600);
        if (mode.body.upward_room>mode.body.y) throw std::runtime_error("Message upward room exceeds canvas");
        mode.name_newline=value.value("name_newline",false);
        if (mode.name_newline && mode.body.height<=mode.body.line_height) throw std::runtime_error("Message body too short for a name line");
        mode.name_prefix=value.value("name_prefix",std::string{}); mode.name_suffix=value.value("name_suffix",std::string{});
        (void)decode_utf8(mode.name_prefix); (void)decode_utf8(mode.name_suffix);
        if (!value.at("images").is_array() || value.at("images").size()>8) throw std::runtime_error("Invalid message UI image list");
        for (const auto& image : value.at("images")) {
            Layer layer; layer.kind=LayerKind::image; layer.slot="message_background"; layer.asset=image.at("asset").get<std::string>();
            const auto& asset=content.asset(layer.asset);
            layer.width=static_cast<float>(asset.width); layer.height=static_cast<float>(asset.height);
            layer.x=metric(image,"x",-800,800); layer.y=metric(image,"y",-600,600);
            mode.images.push_back(std::move(layer));
        }
        modes_.emplace(id,std::move(mode));
    }
}
void MessageUI::set_mode(int mode) {
    if (mode_==mode) return;
    mode_=mode;
    page_message_.reset(); active_message_.reset(); prefix_.clear(); current_text_.clear(); page_labels_.clear(); prefix_characters_=0;
}
void MessageUI::begin_message(int mode,const Message& message) {
    set_mode(mode);
    if (mode!=2) { active_message_=message; return; }
    const bool continuation=page_message_ && page_message_->script==message.script && page_message_->id==message.id;
    std::string prefix;
    auto labels=continuation ? page_labels_ : std::vector<Label>{};
    if (continuation) {
        prefix=prefix_+current_text_;
        if (!prefix.empty() && prefix.back()!='\n' && prefix.back()!='\r') prefix+='\n';
    }
    const auto current=Dialogue(message.text,0).full_text();
    const auto count=decode_utf8(prefix).size();
    if (prefix.size()+current.size()>1024*1024 || count+decode_utf8(current).size()>65536)
        throw std::runtime_error("Full-screen message page exceeds text budget");
    const auto name=display_name(message.name);
    if (!name.empty()) {
        std::size_t label_bytes=name.size();
        for (const auto& label : labels) label_bytes+=label.text.size();
        if (label_bytes>1024*1024) throw std::runtime_error("Full-screen speaker text exceeds page budget");
        if (labels.size()>=1024) throw std::runtime_error("Full-screen speaker count exceeds page budget");
        labels.push_back({name,prefix.size()});
    }
    prefix_=std::move(prefix); current_text_=current; prefix_characters_=count;
    page_message_=message; active_message_=message; page_labels_=std::move(labels);
}
std::string MessageUI::display_name(const std::string& raw) {
    if (raw.starts_with('#')) return "";
    if (raw.starts_with('@')) return "? ? ?";
    return raw;
}
Scene MessageUI::scene(int mode,const std::optional<Message>& message,const std::string& text,
                       std::size_t visible,bool ready,double elapsed) const {
    Scene result; result.title="ef - the first tale."; result.background={0,0,0,1};
    if (!std::isfinite(elapsed) || elapsed<0 || elapsed>1e12) throw std::runtime_error("Invalid message animation clock");
    if (mode==0) return result;
    const auto found=modes_.find(mode);
    if (found==modes_.end()) throw std::runtime_error("No imported layout for panel mode "+std::to_string(mode));
    if (mode!=mode_) return result;
    const auto& layout=found->second;
    result.layers=layout.images;
    if (!message || !active_message_ || active_message_->script!=message->script ||
        active_message_->id!=message->id || active_message_->source_line!=message->source_line) return result;
    auto name=layout.name, body=layout.body;
    const bool page=mode==2 && page_message_ && page_message_->script==message->script &&
        page_message_->id==message->id && page_message_->source_line==message->source_line;
    if (mode==2 && !page) return result; // A mode reset must not resurrect Runtime's previous message.
    if (page) {
        for (const auto& label : page_labels_) {
            auto speaker=name; speaker.text=label.text; speaker.flow_anchor=label.byte_offset;
            result.layers.push_back(std::move(speaker));
        }
        body.text=prefix_+text;
        const auto current_count=decode_utf8(text).size();
        body.visible_characters=prefix_characters_+std::min(visible,current_count);
        body.flow_body=true;
    }
    const auto label=display_name(message->name);
    if (!page && !label.empty()) {
        name.text=layout.name_prefix+label+layout.name_suffix;
        result.layers.push_back(std::move(name));
        if (layout.name_newline) { body.y+=body.line_height; body.height-=body.line_height; }
    }
    if (!page) { body.text=text; body.visible_characters=visible; }
    if (ready && !body.text.empty()) body.advance_marker=TextMarker{advance_asset_,frame_size_,
        static_cast<int>(std::fmod(std::floor(elapsed*1000/frame_ms_),frames_))};
    result.layers.push_back(std::move(body));
    return result;
}
}
