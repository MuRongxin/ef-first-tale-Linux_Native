#include "scene/message_ui.hpp"
#include "text/layout.hpp"
#include <chrono>
#include <cmath>
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
        window.begin_message(1,*message);
        auto scene=window.scene(1,message,"正文",1,false,0);
        require(scene.layers.size()==3 && scene.layers[0].y==417,"normal message panel placement");
        require(scene.layers[1].text=="« 纮 »" && scene.layers[2].y==488,"named message uses a separate first line");
        auto styled_ui=ui; styled_ui["appearance"]={{"outline_width",1.5},{"shadow_y",2},{"baseline_offset",-1}};
        std::ofstream(temp.path/"styled.json")<<styled_ui.dump();
        MessageUI styled(content,"styled.json"); styled.begin_message(1,*message);
        const auto styled_scene=styled.scene(1,message,"正文",1,false,0);
        require(styled_scene.layers[1].appearance.outline_width==1.5f && styled_scene.layers[2].appearance.baseline_offset==-1,"external text appearance applies to name and body");
        styled_ui["appearance"]["outline_width"]=-1; std::ofstream(temp.path/"styled.json")<<styled_ui.dump();
        rejects([&] { MessageUI invalid_style(content,"styled.json"); });
        require(scene.layers[2].text=="正文" && scene.layers[2].visible_characters==1,"full text layout is independent of reveal");
        message->name="#优子"; window.begin_message(3,*message); scene=window.scene(3,message,"字幕",2,true,.2);
        require(scene.layers.size()==3 && scene.layers[0].y==0 && scene.layers[1].y==525,"cinema black bars");
        require(scene.layers[2].x==125 && scene.layers[2].y==530 && scene.layers[2].width==638,"cinema text anchor");
        require(scene.layers[2].advance_marker && scene.layers[2].advance_marker->frame==2,"100ms sprite animation");
        require(window.scene(0,message,"",0,false,0).layers.empty(),"panel zero hides all message UI");
        require(window.scene(1,std::nullopt,"",0,false,0).layers.empty(),"no stale UI before the first message");
        require(MessageUI::display_name("@优子")=="? ? ?" && MessageUI::display_name("#优子").empty(),"speaker identity markers");
        rejects([&] { (void)window.scene(5,message,"",0,false,0); });
        window.set_mode(2);
        Message first{"scr/test.sc","","甲","第一条",10,1};
        window.begin_message(2,first);
        auto page=window.scene(2,first,"第一条",3,true,0);
        require(page.layers.back().text=="第一条" && page.layers.back().flow_body,"first full-screen entry starts a page");
        Message second{"scr/test.sc","","乙","第二条",10,2};
        window.begin_message(2,second);
        page=window.scene(2,second,"第二条",1,false,0);
        require(page.layers.back().text=="第一条\n第二条" && page.layers.back().visible_characters==5,"same L0 appends with an implicit newline and cumulative reveal");
        require(page.layers[1].flow_anchor==0 && page.layers[2].flow_anchor==std::string("第一条\n").size(),"speaker columns anchor to their own paragraph");
        window.set_mode(2); // Repeating the same panel command is a native no-op.
        Message third{"scr/test.sc","","","\\n第三条",10,3};
        window.begin_message(2,third);
        page=window.scene(2,third,"\n第三条",4,true,0);
        require(page.layers.back().text=="第一条\n第二条\n\n第三条","explicit leading newline preserves paragraph spacing");
        Message fourth{"scr/test.sc","","","尾部\\n",11,4};
        window.begin_message(2,fourth);
        page=window.scene(2,fourth,"尾部\n",3,true,0);
        require(page.layers.back().text=="尾部\n","new L0 clears the full-screen page");
        fourth.source_line=5; fourth.text="续条";
        window.begin_message(2,fourth);
        page=window.scene(2,fourth,"续条",2,true,0);
        require(page.layers.back().text=="尾部\n续条","existing line-start cursor avoids a second implicit newline");
        window.set_mode(0); window.set_mode(2);
        page=window.scene(2,fourth,"旧正文",3,true,0);
        require(page.layers.size()==1 && page.layers.front().kind==LayerKind::image,"mode reentry cannot resurrect stale Runtime text while waiting");
        require(window.scene(2,std::nullopt,"",0,false,0).layers.size()==1,"empty full-screen mode still draws its background");
        window.set_mode(1);
        page=window.scene(1,fourth,"旧正文",3,true,0);
        require(page.layers.size()==1 && page.layers.front().kind==LayerKind::image,"leaving full-screen mode must not copy old text into the normal panel");
        window.begin_message(2,second);
        page=window.scene(2,second,"第二条",3,true,0);
        require(page.layers.back().text=="第二条","leaving the mode clears page state even when L0 repeats");
        second.script="scr/other.sc"; window.begin_message(2,second);
        page=window.scene(2,second,"第二条",3,true,0);
        require(page.layers.back().text=="第二条","script domains do not accidentally share a page");
        window.set_mode(0); window.set_mode(2);
        Message bounded{"scr/test.sc","","",std::string(65535,'a'),12,1};
        window.begin_message(2,bounded);
        rejects([&] { window.begin_message(2,Message{"scr/test.sc","","","b",12,2}); });
        MessageUI timed(content,"ui.json");
        timed.transition_to(1,{});
        require(timed.busy() && timed.progress()==0,"panel starts transparent and blocks");
        timed.update(.009); require(timed.progress()==0,"panel alpha is quantized to 10ms ticks");
        timed.update(.001); require(std::abs(timed.progress()-16.0f/255)<1e-6f,"panel alpha advances by 16 per tick");
        timed.update(.14); require(timed.busy() && std::abs(timed.progress()-240.0f/255)<1e-6f,"last incomplete tick");
        timed.update(.01); require(!timed.busy(),"panel finishes at 160ms");
        Message old_page{"scr/test.sc","","姓名","旧文本",70,10};
        timed.begin_message(1,old_page);
        auto old_scene=timed.scene(1,old_page,"旧文本",1,false,0);
        timed.transition_to(2,old_scene); timed.update(.08);
        auto mixed=timed.scene(2,old_page,"旧文本",1,false,0);
        require(mixed.layers.size()==4,"transition retains old background/name/body and new empty panel");
        require(mixed.layers[2].text=="旧文本" && mixed.layers[2].visible_characters==1,"old text reveal is frozen");
        require(std::abs(mixed.layers[0].color[3]-127.0f/255)<1e-6f && std::abs(mixed.layers.back().color[3]-128.0f/255)<1e-6f,"old/new layers use complementary native alpha");
        timed.update(86400); require(!timed.busy(),"headless delta finishes transition");
        auto clean=timed.scene(2,old_page,"旧文本",1,false,0);
        require(clean.layers.size()==1,"completed transition cannot resurrect old runtime text");
        timed.begin_message(2,old_page);
        old_scene=timed.scene(2,old_page,"旧文本",3,false,0);
        timed.transition_to(2,old_scene);
        require(!timed.busy() && timed.scene(2,old_page,"旧文本",3,false,0).layers.back().text=="旧文本","same panel does not clear or wait");
        timed.transition_to(0,old_scene); timed.update(.08);
        require(timed.scene(0,old_page,"旧文本",3,false,0).layers.back().color[3]>0,"hidden target keeps outgoing layers until fade completes");
        timed.update(.08); require(timed.scene(0,old_page,"旧文本",3,false,0).layers.empty(),"hidden panel releases old layers");
        rejects([&] { timed.update(-1); });
        rejects([&] { timed.transition_to(99,{}); });
        require(!timed.busy(),"invalid target leaves previous state intact");
        ui["modes"]["3"]["body"]={125,530,800,70};
        std::ofstream(temp.path/"ui.json")<<ui.dump(); rejects([&] { MessageUI invalid(content,"ui.json"); });
        std::cout<<"Message UI tests PASS: external geometry, panel modes, speaker markers, reveal and animation\n";
        if (argc==2) {
            Content real(argv[1]); ScriptLibrary scripts(real); Face font(real);
            const auto index=real.read_json(real.script_index()); MessageUI original(real,index.at("message_ui").get<std::string>());
            for (char variant : {'e','j'}) {
                int mode=0; std::size_t checked{}; original.set_mode(0);
                const auto program=scripts.compile("scr/000_01mugai.sc",variant);
                for (const auto& instruction : program->instructions) {
                    if (instruction.command==".panel") { mode=std::stoi(instruction.args[0]); original.set_mode(mode); }
                    if (instruction.command!=".message") continue;
                    const auto& a=instruction.args;
                    Dialogue dialogue(a[3],0);
                    const Message entry{instruction.script,a[1],a[2],a[3],std::stoi(a[0]),instruction.line};
                    original.begin_message(mode,entry);
                    const auto view=original.scene(mode,entry,dialogue.full_text(),65536,false,0);
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
            for (char variant : {'e','j'}) {
                std::size_t checked{},segments{};
                for (const auto& [id,path] : scripts.entries()) {
                    (void)path;
                    int mode=0; original.set_mode(0);
                    const auto program=scripts.compile(id,variant);
                    for (const auto& instruction : program->instructions) {
                        if (instruction.command==".panel") {
                            const int next=std::stoi(instruction.args[0]);
                            if (next==2 && mode!=2) ++segments;
                            mode=next; original.set_mode(mode);
                        }
                        if (mode!=2 || instruction.command!=".message") continue;
                        const auto& a=instruction.args;
                        Message entry{instruction.script,a[1],a[2],a[3],std::stoi(a[0]),instruction.line};
                        original.begin_message(2,entry);
                        const auto view=original.scene(2,entry,Dialogue(a[3],0).full_text(),65536,false,0);
                        for (const auto& layer : view.layers) if (layer.kind==LayerKind::text) {
                            const auto layout=layout_text(layer.text,[&](char32_t cp) { return font.advance(cp,layer.font_size); },
                                {layer.width,layer.height,layer.line_height,layer.letter_spacing});
                            if (layout.overflow) throw std::runtime_error(instruction.location()+": full-screen accumulated page overflow");
                        }
                        ++checked;
                    }
                }
                require(checked==35 && segments==8,"full-screen corpus coverage");
                std::cout<<"Full-screen layout PASS: variant="<<variant<<" segments="<<segments<<" messages="<<checked<<" with accumulated pages\n";
            }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
