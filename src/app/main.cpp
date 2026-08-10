#include "app.h"

#include "platform/locale_init.h"

int main(int /*argc*/, char* /*argv*/[])
{
    platform::init_locale();   // before any threads: setlocale is not thread-safe

    app::App application;

    if (!application.init()) {
        return 1;
    }

    application.run();
    application.shutdown();

    return 0;
}
