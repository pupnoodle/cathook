#include <dlfcn.h>

#include <cstdio>

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::fprintf(stderr, "usage: steam_client_load_test /path/to/steamclient.so\n");
    return 2;
  }
  void* module = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (module == nullptr)
  {
    std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
    return 1;
  }
  dlclose(module);
  return 0;
}
