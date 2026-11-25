// the version texts of the build (the header forced in by edplugin_common_version), kept by an export
extern "C" __declspec(dllexport) auto reproducible_version(int which) -> char const *
  {
  if(which == 0)
    return REPRODUCIBLE_VERSION;
  if(which == 1)
    return REPRODUCIBLE_VERSION_COMMIT;
  return REPRODUCIBLE_VERSION_BUILD_TIME;
  }
