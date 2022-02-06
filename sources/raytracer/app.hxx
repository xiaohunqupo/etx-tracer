﻿#pragma once

#include <etx/core/core.hxx>
#include <etx/core/handle.hxx>

#include <etx/render/host/scene_loader.hxx>
#include <etx/rt/integrators/path_tracing.hxx>
#include <etx/rt/rt.hxx>

#include "ui.hxx"
#include "render.hxx"
#include "camera_controller.hxx"

namespace etx {

struct RTApplication {
  RTApplication();

  void init();
  void frame();
  void cleanup();
  void process_event(const sapp_event*);
  void load_scene_file(const std::string&, uint32_t options, bool start_rendering);

 private:
  void on_referenece_image_selected(std::string);
  void on_scene_file_selected(std::string);
  void on_integrator_selected(Integrator*);
  void on_preview_selected();
  void on_run_selected();
  void on_stop_selected(bool wait_for_completion);
  void on_reload_scene_selected();
  void on_reload_geometry_selected();

 private:
  void save_options();

 private:
  RenderContext render;
  UI ui;
  TimeMeasure time_measure;
  SceneRepresentation scene;
  Raytracing raytracing;
  CameraController camera_controller;

  Integrator _test = {raytracing};
  CPUPathTracing _cpu_pt = {raytracing};

  Integrator* _integrator_array[2] = {
    &_test,
    &_cpu_pt,
  };

  Integrator* _current_integrator = nullptr;
  std::string _current_scene_file = {};
  Options _options;
};

}  // namespace etx