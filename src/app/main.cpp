#include "app.h"

int main(int /*argc*/, char* /*argv*/[])
{
    app::App application;

    if (!application.init()) {
        return 1;
    }

    application.run();
    application.shutdown();

    return 0;
}
