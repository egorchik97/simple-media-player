#include "../src/config/Config.h"
#include "LegacyConfig.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs = std::filesystem;
void Check(bool ok, const char* message) { if(!ok) throw std::runtime_error(std::string(message)+" Windows error="+std::to_string(GetLastError())); }
std::string Bytes(const fs::path& p) { std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
int wmain(int argc, wchar_t** argv) {
 try {
  if(argc==3) { media::ConfigManager m(argv[2]); media::Config c; Check(m.Load(c),"child load"); Check(c.theme==4 && c.windowX==-1920 && c.windowPositionSaved,"restart values"); return 0; }
  wchar_t temporary[32768]{}; Check(GetTempPathW(32768,temporary)!=0,"temp path");
  fs::path root=fs::path(temporary)/(L"SMP-config-tests-"+std::to_wstring(GetCurrentProcessId()));
  Check(!fs::exists(root),"unique test root"); fs::create_directories(root);
  auto old=fs::current_path();
  struct Cleanup { fs::path root,cwd; ~Cleanup(){ std::error_code e; fs::current_path(cwd,e); fs::remove_all(root,e); } } cleanup{root,old};
  wchar_t exe[32768]{},system[32768]{}; Check(GetModuleFileNameW(nullptr,exe,32768)!=0,"module"); Check(GetSystemDirectoryW(system,32768)!=0,"system path");
  media::ConfigManager production; const auto stable=production.ConfigPath();
  Check(!stable.empty() && fs::path(stable).is_absolute(),"production absolute path");
  std::wcout<<L"Production path: "<<stable<<std::endl;
  for(auto cwd:{fs::path(system),fs::path(exe).parent_path()}) {
   fs::current_path(cwd); media::ConfigManager again; Check(again.ConfigPath()==stable,"CWD independent production resolver");
   const auto user=root/L"Пользователь_日本語"/L"AppData"/L"Local";
   media::ConfigManager manager(user.wstring()); media::Config c;
   c.theme=4; c.cover=false; c.title=false; c.artist=false; c.pause=false; c.compactPrevious=true; c.compactNext=true;
   c.hover=false; c.pinned=true; c.volume=false; c.asr=true; c.delay=1200; c.opacity=75; c.glow=80; c.roundness=24;
   c.glass=false; c.sourceLyrics=true; c.customBackground=123; c.customAccent=456; c.customText=789; c.customMuted=321;
   c.compactLyrics=true; c.compactWidth=440; c.compactHeight=100; c.compactLyricsHeight=160;
   c.glassStrength=85; c.glassTint=65; c.glassSpeed=75; c.glassAnimated=false; c.windowX=-1920; c.windowY=-100; c.windowPositionSaved=true;
    // Snowfall settings
    c.snowfallEnabled = true;
    c.snowfallCount = 100;
    c.snowfallSpeed = 7;
    c.snowfallSize = 3;
    c.snowfallOpacity = 80;
    c.snowfallWindDirection = 2; // Right
    c.snowfallWindStrength = 75;
    c.snowfallBlur = 2;
   Check(manager.Save(c),"save from launch directory / missing Unicode directories");
   media::Config reloaded; media::ConfigManager restart(user.wstring()); Check(restart.Load(reloaded),"reload");
   const auto before=Bytes(manager.ConfigPath()); Check(restart.Save(reloaded),"reserialize"); Check(before==Bytes(manager.ConfigPath()),"all fields roundtrip");
   Check(reloaded.windowX==-1920 && reloaded.windowY==-100 && reloaded.windowPositionSaved,"signed position");
    Check(reloaded.snowfallEnabled && reloaded.snowfallCount==100 && reloaded.snowfallSpeed==7 && reloaded.snowfallSize==3 && reloaded.snowfallOpacity==80 && reloaded.snowfallWindDirection==2 && reloaded.snowfallWindStrength==75 && reloaded.snowfallBlur==2,"snowfall settings roundtrip");
   std::wstring command=L"\""+std::wstring(exe)+L"\" --reload \""+user.wstring()+L"\"";
   STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
   Check(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,0,nullptr,cwd.c_str(),&si,&pi)!=FALSE,"restart child");
   Check(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0,"child wait"); DWORD code; Check(GetExitCodeProcess(pi.hProcess,&code)!=FALSE,"child exit"); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); Check(code==0,"cross-process reload");
   std::wcout<<L"PASS launch/restart from "<<cwd.wstring()<<std::endl;
   // An exclusive handle deterministically makes replacement unwritable, even under an elevated test runner.
   HANDLE lock=CreateFileW(manager.ConfigPath().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr); Check(lock!=INVALID_HANDLE_VALUE,"lock target");
   c.theme=2; const bool saved=manager.Save(c); DWORD error=GetLastError(); CloseHandle(lock);
   Check(!saved && (error==ERROR_SHARING_VIOLATION || error==ERROR_ACCESS_DENIED),"unwritable target error"); Check(Bytes(manager.ConfigPath())==before,"failed save preserves bytes");
   Check(SetFileAttributesW(manager.ConfigPath().c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,"read-only");
   const bool readonlySaved=manager.Save(c); error=GetLastError(); SetFileAttributesW(manager.ConfigPath().c_str(),FILE_ATTRIBUTE_NORMAL);
   Check(!readonlySaved && error==ERROR_ACCESS_DENIED,"read-only access denied");
   Check(Bytes(manager.ConfigPath())==before,"read-only preserves bytes");
  }
  const auto migrate=root/L"migration"; fs::create_directories(migrate/L"MediaPopup");
  auto legacy=migrate/L"MediaPopup/settings.ini"; PopupConfig original; original.theme=3; original.compactWidth=460;
  Check(SaveConfig(legacy.wstring(),original),"original v0.1.0 SaveConfig"); auto originalBytes=Bytes(legacy);
  media::ConfigManager migration(migrate.wstring()); media::Config migrated; Check(migration.Load(migrated),"migration");
  Check(migrated.theme==3 && migrated.compactWidth==460,"legacy values"); Check(Bytes(migration.ConfigPath())==originalBytes && Bytes(legacy)==originalBytes,"byte preserving migration");
  migrated.theme=5; Check(migration.Save(migrated),"new settings"); original.theme=1; Check(SaveConfig(legacy.wstring(),original),"change old file");
  media::ConfigManager again(migrate.wstring()); media::Config existing; Check(again.Load(existing) && existing.theme==5,"existing target wins");
  const auto blocked=root/L"blocked"; fs::create_directories(blocked); std::ofstream(blocked/L"SimpleMediaPlayer")<<"not a directory";
  media::ConfigManager invalid(blocked.wstring()); Check(!invalid.Save(existing) && GetLastError()!=0,"directory creation error");
  media::ConfigManager relative(L"relative"); Check(!relative.Save(existing) && GetLastError()==ERROR_INVALID_NAME,"no CWD fallback");
  std::cout<<"PASS all-field roundtrip, missing directories, Unicode, unwritable destinations, migration/no-overwrite and child-process restart\n";
  return 0;
 } catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
