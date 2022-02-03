﻿#pragma once

#include <etx/core/core.hxx>
#include <etx/core/handle.hxx>

#include <etx/render/host/scene_loader.hxx>
#include <etx/rt/integrators/path_tracing.hxx>
#include <etx/rt/rt.hxx>

#include "ui.hxx"
#include "render.hxx"

namespace etx {

struct RTApplication {
  void init();
  void frame();
  void cleanup();
  void process_event(const sapp_event*);

 private:
  void on_referenece_image_selected(std::string);
  void on_scene_file_selected(std::string);
  void on_integrator_selected(Integrator*);

 private:
  RenderContext render;
  UI ui;
  TimeMeasure time_measure;
  SceneRepresentation scene;
  Raytracing raytracing;

  Integrator _test = {raytracing};
  CPUPathTracing _cpu_pt = {raytracing};

  Integrator* _integrator_array[2] = {
    &_test,
    &_cpu_pt,
  };

  Integrator* _current_integrator = nullptr;
};

}  // namespace etx
