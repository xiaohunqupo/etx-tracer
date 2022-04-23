#pragma once

#include <etx/render/shared/base.hxx>
#include <etx/render/shared/scene.hxx>

namespace etx {

struct ETX_ALIGNED PTOptions {
  uint32_t iterations ETX_INIT_WITH(1u);
  uint32_t max_depth ETX_INIT_WITH(2u);
  uint32_t rr_start ETX_INIT_WITH(65536u);
};

enum class PTRayPayloadState : uint32_t {
  Unitialized = 0,
  Stopped,
  Running,
  Finished,
};

struct ETX_ALIGNED PTRayPayload {
  Ray ray = {};
  SpectralResponse throughput = {spectrum::kUndefinedWavelength, 1.0f};
  SpectralResponse accumulated = {spectrum::kUndefinedWavelength, 0.0f};
  PTRayPayloadState state = PTRayPayloadState::Stopped;
  uint32_t index = kInvalidIndex;
  uint32_t medium = kInvalidIndex;
  uint32_t path_length = 0u;
  uint32_t iteration = 0u;
  SpectralQuery spect = {};
  float eta = 1.0f;
  float sampled_bsdf_pdf = 0.0f;
  float2 uv = {};
  bool sampled_delta_bsdf = false;
};

ETX_GPU_CODE void init_ray_payload(Sampler& smp, const Scene& scene, uint2 px, uint2 dim, PTRayPayload& payload) {
  payload.spect = spectrum::sample(smp.next());
  payload.uv = get_jittered_uv(smp, px, dim);
  payload.ray = generate_ray(smp, scene, payload.uv);
  payload.throughput = {payload.spect.wavelength, 1.0f};
  payload.accumulated = {payload.spect.wavelength, 0.0f};
  payload.index = px.x + px.y * dim.x;
  payload.medium = scene.camera_medium_index;
  payload.path_length = 1;
  payload.eta = 1.0f;
  payload.sampled_bsdf_pdf = 0.0f;
  payload.sampled_delta_bsdf = false;
}

ETX_GPU_CODE Medium::Sample try_sampling_medium(Sampler& smp, const Scene& scene, PTRayPayload& payload, float max_t) {
  Medium::Sample medium_sample = {};
  if (payload.medium != kInvalidIndex) {
    medium_sample = scene.mediums[payload.medium].sample(payload.spect, smp, payload.ray.o, payload.ray.d, max_t);
    payload.throughput *= medium_sample.weight;
    ETX_VALIDATE(payload.throughput);
  }
  return medium_sample;
}

ETX_GPU_CODE void evaluate_sampled_medium(Sampler& smp, const Scene& scene, const Medium::Sample& medium_sample, const PTOptions& opt, const Raytracing& rt,
  PTRayPayload& payload) {
  const auto& medium = scene.mediums[payload.medium];
  /* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
   * direct light sampling from medium
   * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
  if (payload.path_length + 1 <= opt.max_depth) {
    auto emitter_sample = sample_emitter(payload.spect, smp, medium_sample.pos, scene);
    if (emitter_sample.pdf_dir > 0) {
      auto tr = transmittance(payload.spect, smp, medium_sample.pos, emitter_sample.origin, payload.medium, scene, rt);
      float phase_function = medium.phase_function(payload.spect, medium_sample.pos, payload.ray.d, emitter_sample.direction);
      auto weight = emitter_sample.is_delta ? 1.0f : power_heuristic(emitter_sample.pdf_dir * emitter_sample.pdf_sample, phase_function);
      payload.accumulated += payload.throughput * emitter_sample.value * tr * (phase_function * weight / (emitter_sample.pdf_dir * emitter_sample.pdf_sample));
      ETX_VALIDATE(payload.accumulated);
    }
  }

  float3 w_o = medium.sample_phase_function(payload.spect, smp, medium_sample.pos, payload.ray.d);
  payload.sampled_bsdf_pdf = medium.phase_function(payload.spect, medium_sample.pos, payload.ray.d, w_o);
  payload.sampled_delta_bsdf = false;
  payload.ray.o = medium_sample.pos;
  payload.ray.d = w_o;
}

ETX_GPU_CODE bool handle_hit_ray(Sampler& smp, const Scene& scene, const Intersection& intersection, const PTOptions& opt, const Raytracing& rt, PTRayPayload& payload) {
  const auto& tri = scene.triangles[intersection.triangle_index];
  const auto& mat = scene.materials[tri.material_index];

  if (mat.cls == Material::Class::Boundary) {
    payload.medium = (dot(intersection.nrm, payload.ray.d) < 0.0f) ? mat.int_medium : mat.ext_medium;
    payload.ray.o = shading_pos(scene.vertices, tri, intersection.barycentric, payload.ray.d);
    return true;
  }

  /* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
   * direct light sampling
   * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
  if (payload.path_length + 1 <= opt.max_depth) {
    auto emitter_sample = sample_emitter(payload.spect, smp, intersection.pos, scene);
    if (emitter_sample.pdf_dir > 0) {
      BSDFEval bsdf_eval = bsdf::evaluate({payload.spect, payload.medium, PathSource::Camera, intersection, payload.ray.d, emitter_sample.direction}, mat, scene, smp);
      if (bsdf_eval.valid()) {
        auto pos = shading_pos(scene.vertices, tri, intersection.barycentric, emitter_sample.direction);
        auto tr = transmittance(payload.spect, smp, pos, emitter_sample.origin, payload.medium, scene, rt);
        auto weight = emitter_sample.is_delta ? 1.0f : power_heuristic(emitter_sample.pdf_dir * emitter_sample.pdf_sample, bsdf_eval.pdf);
        payload.accumulated += payload.throughput * bsdf_eval.bsdf * emitter_sample.value * tr * (weight / (emitter_sample.pdf_dir * emitter_sample.pdf_sample));
        ETX_VALIDATE(payload.accumulated);
      }
    }
  }

  /* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
   * directly visible emitters
   * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
  if (tri.emitter_index != kInvalidIndex) {
    const auto& emitter = scene.emitters[tri.emitter_index];
    float pdf_emitter_area = 0.0f;
    float pdf_emitter_dir = 0.0f;
    float pdf_emitter_dir_out = 0.0f;
    auto e = emitter_get_radiance(emitter, payload.spect, intersection.tex, payload.ray.o, intersection.pos,  //
      pdf_emitter_area, pdf_emitter_dir, pdf_emitter_dir_out, scene, (payload.path_length == 0));
    if (pdf_emitter_dir > 0.0f) {
      auto tr = transmittance(payload.spect, smp, payload.ray.o, intersection.pos, payload.medium, scene, rt);
      float pdf_emitter_discrete = emitter_discrete_pdf(emitter, scene.emitters_distribution);
      auto weight = ((payload.path_length == 1) || payload.sampled_delta_bsdf) ? 1.0f : power_heuristic(payload.sampled_bsdf_pdf, pdf_emitter_discrete * pdf_emitter_dir);
      payload.accumulated += payload.throughput * e * tr * weight;
      ETX_VALIDATE(payload.accumulated);
    }
  }

  /* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
   * bsdf sampling
   * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
  auto bsdf_sample = bsdf::sample({payload.spect, payload.medium, PathSource::Camera, intersection, payload.ray.d, {}}, mat, scene, smp);
  if (bsdf_sample.valid() == false) {
    return false;
  }

  if (bsdf_sample.properties & BSDFSample::MediumChanged) {
    payload.medium = bsdf_sample.medium_index;
  }

  ETX_VALIDATE(payload.throughput);
  payload.throughput *= bsdf_sample.weight;
  ETX_VALIDATE(payload.throughput);

  if (payload.throughput.is_zero()) {
    return false;
  }

  if ((payload.path_length >= opt.rr_start) && (apply_rr(payload.eta, smp.next(), payload.throughput) == false)) {
    return false;
  }

  payload.sampled_bsdf_pdf = bsdf_sample.pdf;
  payload.sampled_delta_bsdf = bsdf_sample.is_delta();
  payload.eta *= bsdf_sample.eta;

  payload.ray.d = bsdf_sample.w_o;
  payload.ray.o = shading_pos(scene.vertices, tri, intersection.barycentric, bsdf_sample.w_o);
  return true;
}

ETX_GPU_CODE void handle_missed_ray(const Scene& scene, PTRayPayload& payload) {
  for (uint32_t ie = 0; ie < scene.environment_emitters.count; ++ie) {
    const auto& emitter = scene.emitters[scene.environment_emitters.emitters[ie]];
    float pdf_emitter_area = 0.0f;
    float pdf_emitter_dir = 0.0f;
    float pdf_emitter_dir_out = 0.0f;
    auto e = emitter_get_radiance(emitter, payload.spect, payload.ray.d, pdf_emitter_area, pdf_emitter_dir, pdf_emitter_dir_out, scene);
    ETX_VALIDATE(e);
    if ((pdf_emitter_dir > 0) && (e.is_zero() == false)) {
      float pdf_emitter_discrete = emitter_discrete_pdf(emitter, scene.emitters_distribution);
      auto weight = ((payload.path_length == 1) || payload.sampled_delta_bsdf) ? 1.0f : power_heuristic(payload.sampled_bsdf_pdf, pdf_emitter_discrete * pdf_emitter_dir);
      payload.accumulated += payload.throughput * e * weight;
      ETX_VALIDATE(payload.accumulated);
    }
  }
}

struct ETX_ALIGNED PTGPUData {
  float4* output ETX_EMPTY_INIT;
  Scene scene ETX_EMPTY_INIT;
  PTOptions options ETX_EMPTY_INIT;
};

}  // namespace etx
