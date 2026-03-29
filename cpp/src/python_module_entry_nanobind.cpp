#include "merton_online_calibrator.hpp"
#include "reflection_bind_nanobind.hpp"

#include <nanobind/nanobind.h>

namespace nb = nanobind;
using namespace nb::literals;

NB_MODULE(merton_online_calibrator, m) {
    m.doc() = "Online Merton jump-diffusion calibrator (reflection bindings, nanobind)";

    nb::class_<merton::MertonParams> p(m, "MertonParams");
    p.def(nb::init<>());
    bind_reflected_struct(p);

    nb::class_<merton::CalibratorConfig> cfg(m, "CalibratorConfig");
    cfg.def(nb::init<>());
    bind_reflected_struct(cfg);

    nb::class_<merton::OnlineMertonCalibrator> cl(m, "OnlineMertonCalibrator");
    cl.def(nb::init<merton::MertonParams, merton::CalibratorConfig>(), "initial"_a, "config"_a = merton::CalibratorConfig{});
    bind_reflected_member_functions(cl);
}
