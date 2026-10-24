#pragma once
namespace vr::gameplay::ladders::native
{
    bool initialize();
    bool enabled()noexcept;
    // Native admission is only replaced beside a ladder whose mesh rungs a hand
    // can hold; brush or unrecognised ladders keep the native ladder.
    void set_physical_nearby(bool)noexcept;
}
