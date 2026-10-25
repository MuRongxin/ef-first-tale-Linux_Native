#include "scene/message_ui.hpp"
#include "text/layout.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected rejection"); }
struct Temporary {
    std::filesystem::path path=std::filesystem::current_path()/("test-message-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code error; std::filesystem::remove_all(path,error); }
};
struct Face {
    FT_Library library{}; FT_Face face{};
    explicit Face(const ef::Content& content) {
        if (FT_Init_FreeType(&library)) throw std::runtime_error("FreeType init");
        if (FT_New_Face(library,content.resolve(content.font_path()).c_str(),content.font_face(),&face)) {
            FT_Done_FreeType(library); throw std::runtime_error("FreeType face");
        }
    }
    ~Face() { FT_Done_Face(face); FT_Done_FreeType(library); }
    double advance(char32_t cp,float size) {
        const auto index=FT_Get_Char_Index(face,cp);
        if (!index || FT_Load_Glyph(face,index,FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING)) throw std::runtime_error("Missing audit glyph");
        return static_cast<double>(face->glyph->metrics.horiAdvance)/face->units_per_EM*size;
    }
};
}
int main(int argc,char** argv) {
    try {
        using namespace ef;
        Temporary temp;
        nlohmann::json ui; std::ifstream(EF_UI_FIXTURE)>>ui;
        nlohmann::json manifest{{"version",1},{"design_size",{800,600}},{"development",true},
            {"entry_scene","unused.json"},{"font",{{"path","unused.ttf"}}},{"images",nlohmann::json::object()}};
        const auto add=[&](const std::string& id,int w,int h) {
            manifest["images"][id]={{"design_size",{w,h}},{"variants",{{"1",{{"path","unused.png"},{"pixels",{w,h}}}}}}};
        };
        add("sys/msgPanel.png",800,247); add("sys/fullPanel.png",800,600);
        add("sys/BLACK800x75.png",800,75); add("sys/pauseAnime.png",264,22);
        std::ofstream(temp.path/"manifest.json")<<manifest.dump(); std::ofstream(temp.path/"ui.json")<<ui.dump();
        Content content(temp.path); MessageUI window(content,"ui.json");
        std::optional<Message> message=Message{"scr/test.sc","","纮","正文",1,1};
        auto scene=window.scene(1,message,"正文",1,false,0);
        require(scene.layers.size()==3 && scene.layers[0].y==417,"normal message panel placement");
        require(scene.layers[1].text=="« 纮 »" && scene.layers[2].y==488,"named message uses a separate first line");
        require(scene.layers[2].text=="正文" && scene.layers[2].visible_characters==1,"full text layout is independent of reveal");
        message->name="#优子"; scene=window.scene(3,message,"字幕",2,true,.2);
        require(scene.layers.size()==3 && scene.layers[0].y==0 && scene.layers[1].y==525,"cinema black bars");
        require(scene.layers[2].x==125 && scene.layers[2].y==530 && scene.layers[2].width==638,"cinema text anchor");
        require(scene.layers[2].advance_marker && scene.layers[2].advance_marker->frame==2,"100ms sprite animation");
        require(window.scene(0,message,"",0,false,0).layers.empty(),"panel zero hides all message UI");
        require(window.scene(1,std::nullopt,"",0,false,0).layers.empty(),"no stale UI before the first message");
        require(MessageUI::display_name("@优子")=="? ? ?" && MessageUI::display_name("#优子").empty(),"speaker identity markers");
        rejects([&] { (void)window.scene(5,message,"",0,false,0); });
        ui["modes"]["3"]["body"]={125,530,800,70};
        std::ofstream(temp.path/"ui.json")<<ui.dump(); rejects([&] { MessageUI invalid(content,"ui.json"); });
        std::cout<<"Message UI tests PASS: external geometry, panel modes, speaker markers, reveal and animation\n";
        if (argc==2) {
            Content real(argv[1]); ScriptLibrary scripts(real); Face font(real);
            const auto index=real.read_json(real.script_index()); MessageUI original(real,index.at("message_ui").get<std::string>());
            for (char variant : {'e','j'}) {
                int mode=0; std::size_t checked{};
                const auto program=scripts.compile("scr/000_01mugai.sc",variant);
                for (const auto& instruction : program->instructions) {
                    if (instruction.command==".panel") mode=std::stoi(instruction.args[0]);
                    if (instruction.command!=".message") continue;
                    const auto& a=instruction.args;
                    Dialogue dialogue(a[3],0);
                    const auto view=original.scene(mode,Message{instruction.script,a[1],a[2],a[3],std::stoi(a[0]),instruction.line},dialogue.full_text(),65536,false,0);
                    for (const auto& layer : view.layers) if (layer.kind==LayerKind::text) {
                        const auto layout=layout_text(layer.text,[&](char32_t cp) { return font.advance(cp,layer.font_size); },
                            {layer.width,layer.height+layer.upward_room,layer.line_height,layer.letter_spacing});
                        if (layout.overflow) throw std::runtime_error(instruction.location()+": message region overflow");
                    }
                    ++checked;
                }
                require(checked==427,"opening corpus message count");
                std::cout<<"Opening layout PASS: variant="<<variant<<" messages="<<checked<<" using packaged FreeType font units\n";
            }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
