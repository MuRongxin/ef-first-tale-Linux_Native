#include "script/script.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F&& function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected rejection");
}
std::shared_ptr<const ef::Program> program(const std::string& text, const std::string& id) {
    auto result = std::make_shared<ef::Program>();
    result->id = id;
    result->instructions = ef::parse_script(text, id);
    for (std::size_t i = 0; i < result->instructions.size(); ++i)
        if (result->instructions[i].command == ".label") result->labels[result->instructions[i].args[0]] = i;
    return result;
}
}
int main() {
    try {
        using namespace ef;
        const auto lines = parse_script("\xef\xbb\xbf[e].wait 350\r\n[j].wait 100\r\n.message 100   旁白  保留空格\\n下一行\r\n.message 110 voice-id #名字 对话\r\n..wait 200\r\n制作备注\n", "scr/example.sc");
        require(lines.size() == 6 && lines[0].variant == 'e' && lines[1].variant == 'j', "variants and CRLF");
        require(lines[2].args[1].empty() && lines[2].args[2].empty(), "empty voice and name");
        require(lines[2].args[3] == "旁白  保留空格\\n下一行", "message spacing");
        require(lines[3].args[2] == "#名字" && lines[3].line == 4, "name marker and physical line");
        require(lines[4].command == "..wait" && lines[5].command == "<unparsed>", "unknown lines preserved");
        require(message_display_text("乗n\\n中文\\w\\a\\v") == "乗n\n中文", "display escapes after Unicode conversion");
        rejects([] { (void)parse_script(".message 5 only", "bad"); });
        rejects([] { (void)parse_script(".message 65536   text", "bad"); });
        rejects([] { (void)parse_script(".wait -1", "bad"); });
        rejects([] { (void)parse_script(".if x ?? 1 label", "bad"); });
        rejects([] { (void)parse_script(".select no-colon yes:label", "bad"); });
        rejects([] { (void)parse_script("\xc0\xaf", "bad"); });

        std::map<std::string, std::shared_ptr<const Program>> programs;
        programs["start"] = program(
            ".set score = 1\n.set score = score + 2\n.setGlobal flag = 7\n"
            ".if score == 3 good\n.message 99   wrong\n.label good\n"
            ".message 100   第一条\n.wait 100\n.select 继续:next 结束:stop\n"
            ".label next\n.chain second\n.label stop\n.end\n", "start");
        programs["second"] = program(".if score != 3 error\n.message 200   第二条\n.end\n.label error\n.message 999   wrong\n.end\n", "second");
        const auto loader = [&](const std::string& id) { return programs.at(id); };
        const auto resolver = [](const std::string&, const std::string& target) { return target; };
        ScriptRuntime runtime(loader, resolver, "start");
        runtime.update(0);
        require(runtime.state() == ScriptState::message && runtime.message()->id == 100, "assignment and conditional jump");
        require(runtime.variable("flag") == 7 && runtime.variable("undefined") == 0, "variables");
        runtime.advance(); runtime.update(0);
        require(runtime.state() == ScriptState::waiting, "wait suspends execution");
        runtime.update(.05);
        require(runtime.state() == ScriptState::waiting, "wait partial elapsed");
        runtime.update(.05);
        require(runtime.state() == ScriptState::choices && runtime.choices().size() == 2, "choice pause");
        rejects([&] { runtime.choose(2); });
        runtime.choose(0); runtime.update(0);
        require(runtime.message()->id == 200 && runtime.message()->script == "second", "chain with local variable continuity and source anchors");
        runtime.advance(); runtime.update(0);
        require(runtime.state() == ScriptState::ended, "end");
        programs["loop"] = program(".label loop\n.goto loop\n", "loop");
        ScriptRuntime loop(loader, resolver, "loop");
        rejects([&] { loop.update(0); });
        programs["unknown"] = program(".effect Snow\n.message 1 voice #名字 文字\\w\n.end\n", "unknown");
        ScriptRuntime strict(loader, resolver, "unknown");
        rejects([&] { strict.update(0); });
        ScriptRuntime preview(loader, resolver, "unknown", {'e', true});
        preview.update(0);
        require(preview.message()->id == 1 && preview.diagnostics().size() == 3, "explicit preview diagnostics for effect, voice, timing");
        programs["overflow"] = program(".set x = 9223372036854775807\n.set x = x + 1\n", "overflow");
        ScriptRuntime overflow(loader, resolver, "overflow");
        rejects([&] { overflow.update(0); });
        std::cout << "Script tests PASS: UTF-8/message fields, variants, provenance, variables, branches, waits, choices, chain, diagnostics, loop/overflow guards\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
