#include "merton_online_calibrator.hpp"
#include "reflection_engine.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(merton_online_calibrator, m) {
    m.doc() = "Online Merton jump-diffusion calibrator (reflection bindings)";

    py::class_<jd::MertonParams> p(m, "MertonParams");
    p.def(py::init<>());
    bind_reflected_struct(p);

    py::class_<jd::CalibratorConfig> cfg(m, "CalibratorConfig");
    cfg.def(py::init<>());
    bind_reflected_struct(cfg);

    py::class_<jd::OnlineMertonCalibrator> cl(m, "OnlineMertonCalibrator");
    cl.def(py::init<jd::MertonParams, jd::CalibratorConfig>(), py::arg("initial"), py::arg("config") = jd::CalibratorConfig{});
    bind_reflected_member_functions(cl);
}

