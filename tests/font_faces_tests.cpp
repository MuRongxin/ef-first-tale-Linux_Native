#include "text/font_faces.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected font rejection"); }
}
int main(int argc,char** argv) {
    try {
        if (argc!=3) throw std::runtime_error("Expected fixture content and Latin-only font");
        ef::Content content(argv[1]);
        ef::FontFaces defaults(content);
        ef::FontSettings settings{std::filesystem::absolute(argv[2]),0,1};
        ef::FontFaces custom(content,settings);
        require(custom.for_character(U'A')==custom.primary(),"Latin uses custom face");
        require(custom.for_character(U'中')!=custom.primary(),"Chinese uses packaged fallback");
        require(defaults.for_character(U'中')==defaults.primary(),"Default Chinese face");
        custom.set_pixels(44);
        require(custom.primary()->size->metrics.y_ppem==44,"Custom pixel sizing");
        require(custom.for_character(U'中')->size->metrics.y_ppem==44,"Fallback pixel sizing");
        rejects([&] { (void)custom.for_character(0x10ffff); });
        settings.face=65535; rejects([&] { ef::FontFaces invalid(content,settings); });
        settings.file=content.resolve(content.font_path()); settings.face=content.font_face();
        ef::FontFaces collection(content,settings);
        require(collection.face_count()>1,"Fixture has collection faces");
        settings.face=static_cast<int>((settings.face+1)%collection.face_count());
        ef::FontFaces alternate(content,settings);
        require(alternate.for_character(U'中')==alternate.primary(),"Alternate collection face supports Chinese");
        std::cout << "Font faces PASS: custom Latin, per-character Chinese fallback, physical pixels and TTC selection\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
