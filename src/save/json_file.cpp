#include "save/json_file.hpp"
#include <cerrno>
#include <fstream>
#include <set>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
namespace ef {
namespace { using J=nlohmann::json;void require(bool v,const char* m) {if (!v) throw std::runtime_error(m);} }
nlohmann::json read_json_file(const std::filesystem::path& path,std::size_t max_bytes) {
    require(std::filesystem::file_size(path)<=max_bytes,"JSON file exceeds byte budget");std::ifstream input(path,std::ios::binary);require(static_cast<bool>(input),"Cannot open save");
    std::vector<std::set<std::string>> keys;
    auto callback=[&](int depth,J::parse_event_t event,J& value) {
        require(depth<=64,"Save nesting budget exceeded");
        if (event==J::parse_event_t::object_start) keys.emplace_back();
        else if (event==J::parse_event_t::key) require(keys.back().insert(value.get<std::string>()).second,"Duplicate save key");
        else if (event==J::parse_event_t::object_end) keys.pop_back();
        return true;
    };
    return J::parse(input,callback);
}
void atomic_write_json(const std::filesystem::path& path,const nlohmann::json& j,std::size_t max_bytes) {
    const auto data=j.dump(2)+'\n';require(data.size()<=max_bytes,"JSON file exceeds byte budget");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary=path.string()+".tmp.XXXXXX";const int fd=mkstemp(temporary.data());require(fd>=0,"Cannot create temporary save");bool open=true;
    try {std::size_t done=0;while (done<data.size()) {const auto n=::write(fd,data.data()+done,data.size()-done);if (n<0 && errno==EINTR) continue;require(n>0,"Cannot write save");done+=static_cast<std::size_t>(n);}
        require(::fsync(fd)==0,"Cannot sync save");const int closed=::close(fd);open=false;require(closed==0,"Cannot close save");
        std::filesystem::rename(temporary,path);const auto parent=path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();const int dir=::open(parent.c_str(),O_RDONLY|O_DIRECTORY);if (dir>=0) {::fsync(dir);::close(dir);}
    } catch (...) {if (open) ::close(fd);std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
}
