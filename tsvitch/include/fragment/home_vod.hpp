#pragma once

#include <borealis/core/bind.hpp>
#include <borealis/core/box.hpp>
#include "view/recycling_grid.hpp"

class HomeVod : public brls::Box {
public:
    HomeVod();
    ~HomeVod() override = default;

    void willAppear(bool resetState = false) override;
    void reload();
    void downloadFocused();

    static brls::View* create();

private:
    bool loaded = false;
    bool loading = false;

    BRLS_BIND(RecyclingGrid, recyclingGrid, "nx/vod/grid");
    BRLS_BIND(brls::Label, statusLabel, "nx/vod/status");
};
