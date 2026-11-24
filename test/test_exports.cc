// A dll for first_export_with_prefix's check (test/modules_test.cc): one export by a name with the prefix edpc_.
extern "C" __declspec(dllexport) auto edpc_test_export() -> int { return 1; }
