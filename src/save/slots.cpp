#include "save/slots.hpp"
#include "save/json_file.hpp"
#include <charconv>
#include <cerrno>
#include <set>
#include <stdexcept>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
namespace ef {
namespace {
using J=nlohmann::json;
constexpr std::size_t catalog_budget=256*1024;
void require(bool v,const char* message) {if (!v) throw std::runtime_error(message);}
std::int64_t integer(const J& j,std::int64_t lo,std::int64_t hi) {
    require(j.is_number_integer() && (!j.is_number_unsigned() || j.get<std::uint64_t>()<=static_cast<std::uint64_t>(INT64_MAX)),"Invalid slot integer");
    const auto n=j.get<std::int64_t>();require(n>=lo && n<=hi,"Slot integer out of range");return n;
}
void fields(const J& j,std::initializer_list<const char*> names) {
    require(j.is_object() && j.size()==names.size(),"Invalid slot catalog fields");for (const auto* name:names) require(j.contains(name),"Missing slot catalog field");
}
class Lock {
    int fd_{};
public:
    Lock(const std::filesystem::path& root,bool write) {
        fd_=::open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);require(fd_>=0,"Cannot open slot directory");
        int result;do {result=::flock(fd_,write ? LOCK_EX : LOCK_SH);} while (result<0 && errno==EINTR);
        if (result<0) {::close(fd_);throw std::runtime_error("Cannot lock slot directory");}
    }
    ~Lock() {::flock(fd_,LOCK_UN);::close(fd_);}
    Lock(const Lock&)=delete;
};
bool object_name(const std::string& s) {
    const std::string prefix="objects/slot-";
    if (!s.starts_with(prefix) || !s.ends_with(".json") || s.size()!=prefix.size()+11) return false;
    for (std::size_t n=prefix.size();n<prefix.size()+6;++n) if (!((s[n]>='a' && s[n]<='z') || (s[n]>='A' && s[n]<='Z') || (s[n]>='0' && s[n]<='9'))) return false;
    return true;
}
std::filesystem::path object_path(const std::filesystem::path& root,const std::string& file) {
    require(object_name(file),"Invalid slot object path");
    require(!std::filesystem::is_symlink(root/"objects") && !std::filesystem::is_symlink(root/file),"Slot objects cannot be symlinks");return root/file;
}
J empty_catalog() {return {{"format","ef.slots"},{"version",1},{"sequence",0},{"latest_quick",nullptr},{"entries",J::object()}};}
J catalog(const std::filesystem::path& root) {
    require(!std::filesystem::is_symlink(root/"catalog.json"),"Slot catalog cannot be a symlink");
    if (!std::filesystem::exists(root/"catalog.json")) return empty_catalog();
    auto j=read_json_file(root/"catalog.json",catalog_budget);
    fields(j,{"format","version","sequence","latest_quick","entries"});require(j.at("format")=="ef.slots" && integer(j.at("version"),1,INT64_MAX)==1,"Unsupported slot catalog version");
    const auto sequence=integer(j.at("sequence"),0,INT64_MAX);const auto& entries=j.at("entries");require(entries.is_object() && entries.size()<=109,"Slot catalog entry budget");
    std::set<std::int64_t> sequences;std::set<std::string> objects;std::int64_t latest=0;int latest_id=0;
    for (const auto& [key,e]:entries.items()) {
        const auto id=parse_save_slot(key);require(save_slot_name(id)==key,"Noncanonical slot ID");fields(e,{"file","sequence"});
        require(e.at("file").is_string() && object_name(e.at("file").get<std::string>()),"Invalid slot object reference");
        require(objects.insert(e.at("file").get<std::string>()).second,"Duplicate slot object reference");
        const auto n=integer(e.at("sequence"),1,sequence);require(sequences.insert(n).second,"Duplicate slot generation");
        if (id.quick && n>latest) {latest=n;latest_id=id.number;}
    }
    if (latest_id) require(integer(j.at("latest_quick"),1,9)==latest_id,"Latest quick slot disagrees with catalog");
    else require(j.at("latest_quick").is_null(),"Latest quick slot is absent");
    return j;
}
J load_entry(const std::filesystem::path& root,const J& index,SaveSlot id) {
    const auto key=save_slot_name(id);const auto& entries=index.at("entries");require(entries.contains(key),"Save slot is empty");
    return read_checkpoint(object_path(root,entries.at(key).at("file").get<std::string>()));
}
}
SaveSlot parse_save_slot(const std::string& text) {
    const bool quick=text.starts_with('q');const auto s=std::string_view(text).substr(quick ? 1 : 0);int n{};
    const auto r=std::from_chars(s.data(),s.data()+s.size(),n);require(r.ec==std::errc{} && r.ptr==s.data()+s.size() && n>=1 && n<=(quick ? 9 : 100),"Slot must be 1..100 or q1..q9");return {quick,n};
}
std::string save_slot_name(SaveSlot id) {require(id.number>=1 && id.number<=(id.quick ? 9 : 100),"Invalid save slot");return (id.quick ? "q" : "")+std::to_string(id.number);}
std::filesystem::path default_save_directory() {return default_checkpoint_path().parent_path()/"slots";}
SaveSlots::SaveSlots(std::filesystem::path root) : root_(std::filesystem::absolute(std::move(root)).lexically_normal()) {}
void SaveSlots::save_manual(int n,const nlohmann::json& j) {(void)publish({false,n},j,false);}
SaveSlot SaveSlots::save_quick(const nlohmann::json& j) {return publish({true,1},j,true);}
SaveSlot SaveSlots::publish(SaveSlot id,const J& save,bool rotate) {
    (void)save_slot_name(id);std::filesystem::create_directories(root_);Lock lock(root_,true);auto index=catalog(root_);
    const auto sequence=integer(index.at("sequence"),0,INT64_MAX-1)+1;
    if (rotate) id.number=index.at("latest_quick").is_null() ? 1 : index.at("latest_quick").get<int>()%9+1;
    const auto key=save_slot_name(id);std::string previous;
    if (index.at("entries").contains(key)) previous=index["entries"][key]["file"].get<std::string>();
    require(!std::filesystem::is_symlink(root_/"objects"),"Slot object directory cannot be a symlink");std::filesystem::create_directories(root_/"objects");
    auto pattern=(root_/"objects/slot-XXXXXX.json").string();const int fd=::mkstemps(pattern.data(),5);require(fd>=0,"Cannot allocate slot object");::close(fd);
    const auto file=std::filesystem::path(pattern).lexically_relative(root_).generic_string();
    try {
        write_checkpoint(object_path(root_,file),save);
        index["entries"][key]={{"file",file},{"sequence",sequence}};index["sequence"]=sequence;if (id.quick) index["latest_quick"]=id.number;
        atomic_write_json(root_/"catalog.json",index,catalog_budget);
    } catch (...) {std::error_code ignored;std::filesystem::remove(root_/file,ignored);throw;}
    // Readers hold a shared directory lock until the complete JSON is in memory.
    // Delete only the replaced entry; unknown/orphan files are never swept.
    if (!previous.empty()) {try {std::error_code ignored;std::filesystem::remove(object_path(root_,previous),ignored);} catch (const std::exception&) {}}
    return id;
}
J SaveSlots::load(SaveSlot id) const {Lock lock(root_,false);return load_entry(root_,catalog(root_),id);}
J SaveSlots::load_latest_quick() const {Lock lock(root_,false);const auto index=catalog(root_);require(!index.at("latest_quick").is_null(),"No quick save exists");return load_entry(root_,index,{true,index.at("latest_quick").get<int>()});}
J SaveSlots::list(const Content& content) const {
    J index=empty_catalog(),slots=J::array();
    const auto scan=[&] {
        for (const bool quick:{false,true}) for (int n=1;n<=(quick ? 9 : 100);++n) {
            const SaveSlot id{quick,n};const auto key=save_slot_name(id);J row={{"slot",key},{"kind",quick ? "quick" : "manual"},{"number",n},{"status","empty"}};
            if (index.at("entries").contains(key)) {row["sequence"]=index["entries"][key]["sequence"];
                try {row["metadata"]=checkpoint_metadata(content,load_entry(root_,index,id));row["status"]=row["metadata"]["compatible"]==true ? "compatible" : "incompatible";}
                catch (const std::exception& e) {row["status"]="unreadable";row["error"]=e.what();}
            }
            slots.push_back(std::move(row));
        }
    };
    if (std::filesystem::exists(root_)) {Lock lock(root_,false);index=catalog(root_);scan();} else scan();
    return {{"format","ef.slots.summary"},{"version",1},{"sequence",index.at("sequence")},{"latest_quick",index.at("latest_quick")},{"validation_scope","metadata and script/layout binding; full state/media checked on load"},{"slots",slots}};
}
}
