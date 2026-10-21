// Read-only inventory: intentionally has no deletion or filesystem-write API.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
namespace fs=std::filesystem;
struct File{std::string id,category,path,risk,reason;std::uintmax_t bytes;};
std::string quote(const std::string& in){
  const char* h="0123456789abcdef";std::string out="\"";
  for(unsigned char c:in){switch(c){
    case '"':out+="\\\"";break;case '\\':out+="\\\\";break;
    case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;
    default: if(c<32){out+="\\u00";out+=h[c>>4];out+=h[c&15];}else out+=char(c);
  }}return out+"\"";
}
std::string lower(std::string v){std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return char(std::tolower(c));});return v;}
class Scanner{
  fs::path root;std::vector<File> files;std::vector<std::string> warnings;std::size_t visited=0;
  void walk(const std::string& sub,const std::string& kind){
    const fs::path dir=root/sub;std::error_code ec;auto st=fs::symlink_status(dir,ec);
    if(ec||!fs::is_directory(st)||fs::is_symlink(st)){warnings.push_back(sub+" : absent/inaccessible ou lien symbolique (chemin non confirmé)");return;}
    fs::recursive_directory_iterator it(dir,fs::directory_options::skip_permission_denied,ec),end;
    if(ec){warnings.push_back(sub+" : échec d'ouverture");return;}
    while(it!=end){
      if(++visited>20000){warnings.push_back("Inventaire partiel : limite de 20 000 entrées");return;}
      if(it.depth()>12)it.disable_recursion_pending();
      const auto entry=*it;std::error_code se;auto status=entry.symlink_status(se);
      if(!se&&fs::is_regular_file(status)){
        auto ext=lower(entry.path().extension().string());std::string category,risk,reason;
        if((kind=="photo"&&(ext==".jpg"||ext==".jpeg"||ext==".png"||ext==".jxr"))||
           (kind=="video"&&(ext==".mp4"||ext==".webm"))){
          category="media";risk="review";reason="Capture personnelle, confirmation nécessaire";
        }else if(kind=="etahen"){
          if(ext==".log"){category="logs";risk="review";reason="Journal etaHEN : archiver avant décision";}
          else if(ext==".ini"||ext==".json"||ext==".elf"||ext==".sprx"){
            category="residuals";risk="protected";reason="Fichier etaHEN : présence ne prouve pas qu'il soit orphelin";
          }
        }
        if(!category.empty()){
          const auto size=entry.file_size(se);
          if(!se)files.push_back({std::to_string(files.size()+1),category,entry.path().generic_string(),risk,reason,size});
        }
      }
      it.increment(ec);
      if(ec){warnings.push_back(sub+" : analyse partielle (erreur d'accès)");ec.clear();}
    }
  }
public:
  explicit Scanner(fs::path p):root(std::move(p)){}
  void scan(){walk("user/av_contents/photo/NPXS40087","photo");walk("user/av_contents/video/NPXS40087","video");walk("data/etaHEN","etahen");
    std::sort(files.begin(),files.end(),[](const File&a,const File&b){return a.bytes>b.bytes;});}
  void output()const{
    std::cout<<"{\"schemaVersion\":1,\"source\":\"CleanMyPS5 read-only C++ scanner\",\"warnings\":[";
    for(size_t i=0;i<warnings.size();i++){if(i)std::cout<<",";std::cout<<quote(warnings[i]);}
    std::cout<<"],\"items\":[";
    for(size_t i=0;i<files.size();i++){if(i)std::cout<<",";const auto&f=files[i];
      std::cout<<"{\"id\":"<<quote(f.id)<<",\"category\":"<<quote(f.category)
        <<",\"path\":"<<quote(f.path)<<",\"bytes\":"<<f.bytes
        <<",\"risk\":"<<quote(f.risk)<<",\"reason\":"<<quote(f.reason)<<"}";
    }std::cout<<"]}\n";
  }
};
int main(int argc,char**argv){
  if(argc!=3||std::string(argv[1])!="--root"){std::cerr<<"Usage: cleanmyps5-scan --root <mounted-filesystem>\n";return 2;}
  std::error_code ec;fs::path root=argv[2];
  if(!fs::is_directory(root,ec)||ec||fs::is_symlink(fs::symlink_status(root,ec))){std::cerr<<"Invalid root\n";return 2;}
  Scanner s(root);s.scan();s.output();return 0;
}
