#include "Core/studio_document.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace studio;namespace fs=std::filesystem;
static void need(bool ok,const std::string &why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    auto doc=Document::empty();std::string error,report;unsigned char rgba[8*4*4];memset(rgba,255,sizeof rgba);int id=-1;need(doc.import_rgba("WHITE",8,4,rgba,error,id),error);doc.place(id,0,0,{20,0});doc.place(id,0,0,{-10,0});
    auto &state=const_cast<State&>(doc.state());auto bank=std::make_shared<AssetBank>(*state.assets);strcpy(bank->data.palettes[0].name,"STAGEP");bank->data.palettes[0].count=95;state.assets=bank;
    bank->data.palettes.push_back(bank->data.palettes[0]);strcpy(bank->data.palettes[1].name,"OTHERP");state.objects[1].object.fl=1;
    bool fade=false;for(auto &i:doc.validate())if(i.message.find("94-color")!=std::string::npos){need(!i.error,"Fade condition must stay contextual");fade=true;}need(fade,"Missing 94-color fade notice");
    state.objects[1].object.fl=0;bank->data.palettes.pop_back();bank->data.palettes[0].count=94;fade=false;for(auto &i:doc.validate())fade|=i.message.find("94-color")!=std::string::npos;need(!fade,"94 colors should pass");
    auto root=fs::temp_directory_path()/("bdd-mk2-safe-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directory(root);auto target=root/"safe.BDB";
    bank->data.palettes.push_back(bank->data.palettes[0]);strcpy(bank->data.palettes[1].name,"stagep");bool duplicate=false;for(auto &i:doc.validate())duplicate|=i.error&&i.message.find("Duplicate stage palette")!=std::string::npos;need(duplicate,"Palette collision not diagnosed");
    need(!doc.save_game_sources(target.u8string(),report,error)&&!fs::exists(target)&&!fs::exists(root/"safe.BDD"),"Invalid export created files");bank->data.palettes.pop_back();
    auto original=state.objects;need(doc.save_game_sources(target.u8string(),report,error),error);Document reopened;need(reopened.load(target.u8string(),error),error);need(reopened.state().objects[0].object.depth<reopened.state().objects[1].object.depth,"Game output not sorted by X");need(state.objects[0].object.order==original[0].object.order,"Source editor order changed");need(reopened.state().assets->data.palettes[0].name==std::string("STAGEP"),"Palette order/name changed");
    if(fs::weakly_canonical(root).parent_path()==fs::weakly_canonical(fs::temp_directory_path()))fs::remove_all(root);
    std::cout<<"BDDTool MK2 palette/export safety passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
