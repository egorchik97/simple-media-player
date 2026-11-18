#include <windows.h>
#include <winrt/Windows.Foundation.h>

#include "../src/core/Application.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int showCmd) {
    // Initialize COM/MTA for the main thread before any WinRT usage.
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    int result;
    {
        media::Application app;
        result = app.Run(instance, arguments, showCmd);
    }
    winrt::uninit_apartment();
    return result;
}
