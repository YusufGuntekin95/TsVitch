#pragma once

#include <borealis/core/bind.hpp>
#include <borealis/core/box.hpp>
#include "view/recycling_grid.hpp"
#include "utils/xtream_helper.hpp"

class HomeSeries : public brls::Box {
public:
    HomeSeries();
    ~HomeSeries() override = default;

    void willAppear(bool resetState = false) override;
    void reload();
    void openSeries(const tsvitch::XtreamSeries& series);
    void downloadFocused();

    static brls::View* create();

private:
    void showEpisodePicker(const tsvitch::XtreamSeries& series,
                           const tsvitch::XtreamSeriesInfo& info,
                           bool downloadMode);

    bool loaded = false;
    bool loading = false;

    BRLS_BIND(RecyclingGrid, recyclingGrid, "nx/series/grid");
    BRLS_BIND(brls::Label, statusLabel, "nx/series/status");
};
