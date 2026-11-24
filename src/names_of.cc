// edplugin_common - both names of a shader's bytecode under a fault handler (include/edplugin_common/shader/names_of.h).
#include <edplugin_common/shader/names_of.h>

#include <edplugin_common/guard/guard.h>
#include <edplugin_common/shader/shader.h>

#include <windows.h>

namespace edplugin_common
  {
  namespace
    {
    ///\brief the reads in a function of its own: clang-cl catches a fault under __try only from a call (without /EHa a
    /// fault of an instruction in the __try body itself is not caught)
    [[gnu::noinline]]
    auto both_names(void const * code, std::size_t bytes) noexcept -> names_t
      {
      return {fnv1a64(code, bytes), fnv_3dmigoto(code, bytes), true};
      }
    }  // namespace

  auto names_of(void const * code, std::size_t bytes) noexcept -> names_t
    {
    if(not code)
      return {0, 0, false};
    // no object with a destructor in this function: a fault unwinds nothing but the frame. The filter always handles
    // the fault: one let through this noexcept function would end the process
    __try
      {
      return both_names(code, bytes);
      }
    __except(fault_action(GetExceptionCode()))
      {
      return {0, 0, false};
      }
    }
  }  // namespace edplugin_common
