// MSVC debug builds report failed assert()s through a dialog box, which in a test run (and on
// CI) means a silent abort with exit code 3 and no message. Route CRT assert/error reports to
// stderr instead so the failing expression, file and line are visible.

#if defined(_MSC_VER) && defined(_DEBUG)

#include <crtdbg.h>

#include <initializer_list>

namespace {

struct CrtReportToStderr {
    CrtReportToStderr() noexcept {
        for (const int type : {_CRT_ASSERT, _CRT_ERROR}) {
            _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
        }
    }
};

// Runs before main(); constructing it has no other effect.
const CrtReportToStderr kCrtReportToStderr{};

} // namespace

#endif
