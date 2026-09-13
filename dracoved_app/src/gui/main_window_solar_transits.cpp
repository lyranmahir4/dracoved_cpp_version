#include "main_window.h"
#include "solar_transit_panel.h"
#include "chart_wheel_widget.h"
#include <QSpinBox>

namespace dracoved {
void MainWindow::clearSolarTransitOverlay() {
    if (!solarTransitOverlayActive_ || !chartWheel_) return;
    solarTransitOverlayActive_ = false;
    // Some destinations only refresh their docks and keep the existing wheel.
    // Remove the overlay here, before the destination renders its own chart.
    if (hasSolarChart_) chartWheel_->setChart(currentSolarChart_, currentSolarInput_.houseSystem);
    else chartWheel_->clearChart();
    chartWheel_->clearHighlight();
    chartWheel_->setBaseLabel("Natal");
    chartWheel_->setOverlayLabel("Transit");
    chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
    chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
}

bool MainWindow::provideSolarTransitSource(SolarTransitSource* source, QString* error) {
    if (!hasCurrentChart_ || !hasSolarChart_ || solarPending_) {
        *error = "Calculate the Solar Return with the current settings first. This subtab uses that chart as its fixed reference.";
        return false;
    }
    if (!currentSolarChart_.utcDateTime.isValid() || !swe_.isLoaded() || ephePath_.isEmpty()) {
        *error = "The return moment or ephemeris is unavailable. Recalculate the Solar Return first.";
        return false;
    }
    double target = 0;
    if (!solarReturnTargetSunLongitude(&target, error)) return false;
    source->returnChart = currentSolarChart_;
    source->returnInput = currentSolarInput_;
    source->natalInput = currentInput_;
    source->natalSunLongitude = target;
    source->returnYear = solarYearSpin_ ? solarYearSpin_->value() : currentSolarChart_.localDateTime.date().year();
    source->tajaka = solarChartMethod() == SolarChartMethod::Tajaka;
    source->location = currentSolarLocation_;
    source->dllPath = swe_.loadedPath();
    source->ephePath = ephePath_;
    return true;
}
} // namespace dracoved
