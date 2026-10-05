#include "adapter.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace { unsigned checks=0; bool logged=false;
void check(bool ok,const char* name) { if(!ok) { std::fprintf(stderr,"FAIL: %s\n",name); std::exit(1); } ++checks; std::printf("PASS: %s\n",name); }
void logger(const char* text) { logged=std::strstr(text,"NOT APPLIED")!=nullptr; }
}
int main() {
  const auto root=std::filesystem::temp_directory_path()/(L"enshrouded-adapter-check-"+std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directory(root);
  const auto input=root/L"fixture.bin";
  { std::ofstream output(input,std::ios::binary); output<<"abc"; }
  check(xhl::verify_file(input.wstring(),"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),"verified SHA-256 file accepted");
  check(!xhl::verify_file(input.wstring(),xhl::server_hash),"wrong server content refused");
  check(!xhl::verify_file((root/L"missing").wstring(),xhl::plugin_hash),"missing dependency refused");
  check(!xhl::validate_server(GetModuleHandleW(nullptr)),"unrelated executable rejected without hooks");
  check(!xhl::start(GetModuleHandleW(nullptr),input.wstring(),logger),"adapter fails closed on unsupported executable");
  check(logged,"compatibility rejection logged");
  check(!xhl::start(GetModuleHandleW(nullptr),input.wstring(),logger),"initialization cannot run twice");
  std::filesystem::remove(input);std::filesystem::remove(root);
  std::printf("%u cloud checks passed; game integration not run.\n",checks);
}
