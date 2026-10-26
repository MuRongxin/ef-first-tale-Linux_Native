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
        const auto notes=parse_script("[j]；(表情注意）\n[e]；☆（演出备注）\n；本地备注\n.message 7   ；正文保留\n[j]未知正文\n.unknown 1\n", "notes");
        require(notes.size()==3 && notes[0].line==4 && notes[0].args[3]=="；正文保留","Production notes must preserve physical lines and message text");
        require(notes[1].command=="<unparsed>" && notes[2].command==".unknown","Note compatibility must not swallow other unknown input");
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
        runtime.update(.5);
        require(runtime.state() == ScriptState::waiting, "wait partial elapsed");
        runtime.update(.5);
        require(runtime.state() == ScriptState::choices && runtime.choices().size() == 2, "choice pause");
        rejects([&] { runtime.choose(2); });
        runtime.choose(0); runtime.update(0);
        require(runtime.message()->id == 200 && runtime.message()->script == "second", "chain with local variable continuity and source anchors");
        runtime.advance(); runtime.update(0);
        require(runtime.state() == ScriptState::ended, "end");
        ScriptRuntime excerpt(loader, resolver, "start", {'e',false,7});
        excerpt.update(0);
        require(excerpt.message()->id == 100 && excerpt.variable("score") == 0,"development start line preserves source anchors without replaying state");
        rejects([&] { ScriptRuntime invalid(loader,resolver,"start",{'e',false,999}); });
        const auto compatibility = parse_script("[j]:.wait 50\n[e]:.stage * test.png 0 0\n", "colon");
        require(compatibility[0].variant=='j' && compatibility[0].command==".wait" && compatibility[0].raw=="[j]:.wait 50", "colon variant compatibility preserves provenance");
        programs["literal"] = program(".message 1   字面\\\\v\n", "literal");
        ScriptRuntime literal(loader,resolver,"literal"); literal.update(0);
        require(literal.displayed_text()=="字面\\v","escaped backslash does not require a voice host");
        programs["timed"] = program(".message 1   前\\a后\n.message 2   一\\w二\\v三\n.message 3   末\n.end\n", "timed");
        ScriptRuntime timed(loader,resolver,"timed",{'e',false,0,10});
        bool speaking = true;
        timed.set_host({{}, {}, {}, {}, [&] { return speaking; }});
        timed.update(0);
        require(timed.displayed_text().empty() && timed.message_serial()==1,"typewriter starts without exposing full source text");
        timed.advance();
        require(timed.displayed_text()=="前后" && timed.state()==ScriptState::message,"fast reveal preserves automatic message event");
        timed.update(0);
        require(timed.message()->id==2 && timed.message_serial()==2,"automatic message resumes script after all text");
        timed.advance(); timed.advance();
        require(timed.displayed_text()=="一" && !timed.message_complete(),"click cannot remove fixed inline wait");
        timed.update(.2);
        require(timed.displayed_text()=="一二" && !timed.message_complete(),"voice barrier stops later text");
        speaking = false; timed.update(.1);
        require(timed.displayed_text()=="一二三" && timed.message_complete(),"voice completion releases remaining text");
        timed.advance(); timed.update(0);
        require(timed.message()->id==3 && timed.message_serial()==3,"manual completion resumes once");
        programs["locked"] = program(".pragma disable_control\n.message 1   锁定\n.pragma enable_control\n.message 2   正常\n", "locked");
        ScriptRuntime locked(loader,resolver,"locked");
        speaking = true; locked.set_host({{}, {}, {}, {}, [&] { return speaking; }});
        locked.update(0); locked.advance(); locked.update(0);
        require(!locked.controls_enabled() && locked.message()->id==1,"disabled controls ignore clicks and wait for voice");
        speaking = false; locked.update(0);
        require(locked.controls_enabled() && locked.message()->id==2,"scripted controls resume automatically after voice ends");
        programs["observer"]=program(".set finderCounter = 856\n.set finderCounter = 856\n.setGlobal other = 1\n.message 1   pause\n.end\n","observer");
        ScriptRuntime observer(loader,resolver,"observer");int assignments=0;
        observer.set_host({{}, {}, {}, {}, {}, [&](const std::string& name,std::int64_t value) {
            require(name=="finderCounter" && value==856,"Unexpected local assignment notification");++assignments;
        }});
        observer.update(0);require(assignments==2,"Repeated same-value assignments must notify the host");
        observer.update_local_variable("finderCounter",857);
        observer.update_local_variable("missing",42);
        observer.update_local_variable("other",42);
        require(observer.variable("finderCounter")==857 && observer.variable("missing")==0 && observer.variable("other")==1 && assignments==2,
            "Clock writeback must only update existing locals without recursive notification");
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
        programs["hosted"] = program(".stage * test.png 0 0\n.message 1 voice #名字 文字\n.wait 100\n.end\n", "hosted");
        ScriptRuntime hosted(loader, resolver, "hosted");
        double remaining{}, elapsed{};
        int commands{}, voices{};
        hosted.set_host({[&](const Instruction& instruction) {
            if (instruction.command != ".stage") return false;
            ++commands; remaining = .2; return true;
        }, [&](const Message& message) { require(message.voice == "voice", "voice routed through host"); ++voices; },
        [&](double seconds) { elapsed += seconds; remaining = std::max(0.0, remaining-seconds); },
        [&] { return remaining > 0; }});
        hosted.update(0);
        require(commands == 1 && voices == 0 && hosted.state() == ScriptState::running, "host command blocks following instructions");
        hosted.update(.1);
        require(voices == 0, "visual transition remains asynchronous");
        hosted.update(.1);
        require(voices == 1 && hosted.state() == ScriptState::message, "message and voice begin after visual completion");
        hosted.update(.1);
        require(elapsed > .29 && voices == 1, "host ticks while message is paused without replaying voice");
        hosted.advance(); hosted.update(0); hosted.update(1);
        require(hosted.state() == ScriptState::ended, "host and core waits cooperate");
        std::cout << "Script tests PASS: UTF-8/message fields, variants, provenance, variables, branches, waits, choices, chain, diagnostics, loop/overflow guards\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
