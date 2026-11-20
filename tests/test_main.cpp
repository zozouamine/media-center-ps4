#include "test.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace test {
std::string root;unsigned assertions=0;
std::vector<Case>& cases() { static std::vector<Case> list;return list; }
void expect(bool condition,const char* expression,const char* file,int line) {
    ++assertions;if (!condition) throw std::runtime_error(std::string(file)+":"+std::to_string(line)+" "+expression);
}
void wait(const std::function<bool()>& predicate,double timeout) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::duration<double>(timeout);
    while (!predicate()) { if (std::chrono::steady_clock::now()>deadline) throw std::runtime_error("Timed out waiting for asynchronous result");std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
}
Source fixture_source() { Source source;std::string error;if (!source_from_path(root+"/tests/generated/usb-demo",source,error)) throw std::runtime_error(error);return source; }
Item item(const Source& source,const std::string& relative) {
    auto slash=relative.find_last_of('/');auto name=slash==std::string::npos?relative:relative.substr(slash+1);Item result{source,{name,relative,classify(name),0,0}};
    ReadOnlyFile f;std::string error;if (f.open(source,relative,error)) result.entry.size=f.size();return result;
}
std::vector<uint8_t> bytes(const std::string& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);if (!stream) throw std::runtime_error("Missing fixture: "+path);size_t size=static_cast<size_t>(stream.tellg());std::vector<uint8_t> result(size);stream.seekg(0);stream.read(reinterpret_cast<char*>(result.data()),static_cast<std::streamsize>(size));return result;
}
}
static std::string escape(const std::string& s) {std::string out;for(char c:s) {if(c=='"'||c=='\\') out+='\\';if(c=='\n') out+="\\n";else out+=c;}return out;}
int main(int argc,char** argv) {
    test::root=argc>1?argv[1]:".";unsigned failed=0;std::ostringstream json;json<<"{\n  \"target\": \"computer portable-core tests\",\n  \"hardware_tested\": false,\n  \"tests\": [\n";
    for(size_t i=0;i<test::cases().size();++i) {
        const auto& c=test::cases()[i];std::string error;unsigned before=test::assertions;auto start=mc::monotonic_now();
        try {c.body();}catch(const std::exception& e) {error=e.what();++failed;}
        std::printf("%s %s%s%s\n",error.empty()?"PASS":"FAIL",c.name.c_str(),error.empty()?"":" — ",error.c_str());
        json<<"    {\"name\": \""<<escape(c.name)<<"\", \"passed\": "<<(error.empty()?"true":"false")<<", \"assertions\": "<<test::assertions-before
            <<", \"seconds\": "<<mc::monotonic_now()-start<<", \"error\": \""<<escape(error)<<"\"}"<<(i+1==test::cases().size()?"\n":",\n");
    }
    json<<"  ],\n  \"passed\": "<<(failed?"false":"true")<<",\n  \"assertions\": "<<test::assertions<<",\n  \"failed\": "<<failed<<"\n}\n";
    std::ofstream out(test::root+"/reports/unit-test-results.json");out<<json.str();std::printf("%zu tests, %u assertions, %u failures\n",test::cases().size(),test::assertions,failed);return failed?1:0;
}
