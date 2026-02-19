#include "merton_online_calibrator.hpp"
#include "reflection_engine.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(merton_online_calibrator, m) {
    m.doc() = "Online Merton jump-diffusion calibrator (reflection bindings)";

    py::class_<merton::MertonParams> p(m, "MertonParams");
    p.def(py::init<>());
    bind_reflected_struct(p);

    py::class_<merton::CalibratorConfig> cfg(m, "CalibratorConfig");
    cfg.def(py::init<>());
    bind_reflected_struct(cfg);

    py::class_<merton::OnlineMertonCalibrator> cl(m, "OnlineMertonCalibrator");
    cl.def(py::init<merton::MertonParams, merton::CalibratorConfig>(), py::arg("initial"), py::arg("config") = merton::CalibratorConfig{});
    bind_reflected_member_functions(cl);
}

