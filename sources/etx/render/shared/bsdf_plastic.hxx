﻿namespace etx {

namespace PlasticBSDF {

ETX_GPU_CODE BSDFSample sample_delta(Sampler& smp, const BSDFData& data, const Scene& scene) {
  Frame frame;
  if (data.check_side(frame) == false) {
    return {{data.spectrum_sample.wavelength, 0.0f}};
  }

  auto eta_e = data.material.ext_ior(data.spectrum_sample).eta.monochromatic();
  auto eta_i = data.material.int_ior(data.spectrum_sample).eta.monochromatic();
  auto f = fresnel::dielectric(data.spectrum_sample, data.w_i, frame.nrm, eta_e, eta_i);

  bool reflection = smp.next() <= f.monochromatic();

  BSDFSample result;
  result.properties = BSDFSample::Diffuse;
  if (reflection) {
    result.w_o = normalize(reflect(data.w_i, frame.nrm));
    result.properties = result.properties | BSDFSample::DeltaReflection;
  } else {
    result.w_o = sample_cosine_distribution(smp.next(), smp.next(), frame.nrm, 1.0f);
  }

  float n_dot_o = dot(frame.nrm, result.w_o);
  auto diffuse = bsdf::apply_image(data.spectrum_sample, data.material.diffuse(data.spectrum_sample), data.material.diffuse_image_index, data.tex, scene);

  if (reflection) {
    auto bsdf = diffuse * (kInvPi * n_dot_o * (1.0f - f)) + data.material.specular(data.spectrum_sample) * f;
    result.pdf = kInvPi * n_dot_o * (1.0f - f.monochromatic()) + f.monochromatic();
    result.weight = bsdf / result.pdf;
  } else {
    result.pdf = kInvPi * n_dot_o;
    result.weight = diffuse;
  }

  return result;
}

ETX_GPU_CODE BSDFEval evaluate_delta(const BSDFData& data, const Scene& scene) {
  Frame frame;
  if (data.check_side(frame) == false) {
    return {data.spectrum_sample.wavelength, 0.0f};
  }

  float n_dot_o = dot(frame.nrm, data.w_o);
  if (n_dot_o <= kEpsilon) {
    return {data.spectrum_sample.wavelength, 0.0f};
  }

  float3 m = normalize(data.w_o - data.w_i);
  auto eta_e = data.material.ext_ior(data.spectrum_sample).eta.monochromatic();
  auto eta_i = data.material.int_ior(data.spectrum_sample).eta.monochromatic();
  auto f = fresnel::dielectric(data.spectrum_sample, data.w_i, m, eta_e, eta_i);

  auto diffuse = bsdf::apply_image(data.spectrum_sample, data.material.diffuse(data.spectrum_sample), data.material.diffuse_image_index, data.tex, scene);

  BSDFEval result;
  result.func = diffuse * (kInvPi * (1.0f - f.monochromatic()));
  ETX_VALIDATE(result.func);
  result.bsdf = diffuse * (kInvPi * n_dot_o * (1.0f - f.monochromatic()));
  ETX_VALIDATE(result.bsdf);
  result.weight = diffuse;
  ETX_VALIDATE(result.weight);
  result.pdf = kInvPi * n_dot_o * (1.0f - f.monochromatic());
  ETX_VALIDATE(result.pdf);
  return result;
}

ETX_GPU_CODE float pdf_delta(const BSDFData& data, const Scene& scene) {
  Frame frame;
  if (data.check_side(frame) == false) {
    return 0.0f;
  }

  float n_dot_o = dot(frame.nrm, data.w_o);
  if (n_dot_o <= kEpsilon) {
    return 0.0f;
  }

  float3 m = normalize(data.w_o - data.w_i);
  auto eta_e = data.material.ext_ior(data.spectrum_sample).eta.monochromatic();
  auto eta_i = data.material.int_ior(data.spectrum_sample).eta.monochromatic();
  auto f = fresnel::dielectric(data.spectrum_sample, data.w_i, m, eta_e, eta_i);

  return kInvPi * n_dot_o * (1.0f - f.monochromatic());
}

ETX_GPU_CODE BSDFSample sample(Sampler& smp, const BSDFData& data, const Scene& scene) {
  if (dot(data.material.roughness, float2{0.5f, 0.5f}) <= kDeltaAlphaTreshold) {
    return sample_delta(smp, data, scene);
  }

  Frame frame;
  if (data.check_side(frame) == false) {
    return {{data.spectrum_sample.wavelength, 0.0f}};
  }

  auto ggx = bsdf::NormalDistribution(frame, data.material.roughness);
  auto m = ggx.sample(smp, data.w_i);

  auto eta_e = data.material.ext_ior(data.spectrum_sample).eta.monochromatic();
  auto eta_i = data.material.int_ior(data.spectrum_sample).eta.monochromatic();
  auto f = fresnel::dielectric(data.spectrum_sample, data.w_i, m, eta_e, eta_i);

  uint32_t properties = 0;
  BSDFData eval_data = data;
  if (smp.next() <= f.monochromatic()) {
    eval_data.w_o = normalize(reflect(data.w_i, m));
  } else {
    eval_data.w_o = sample_cosine_distribution(smp.next(), smp.next(), frame.nrm, 1.0f);
    properties = BSDFSample::Diffuse;
  }

  return {eval_data.w_o, evaluate(eval_data, scene), properties};
}

ETX_GPU_CODE BSDFEval evaluate(const BSDFData& data, const Scene& scene) {
  if (dot(data.material.roughness, float2{0.5f, 0.5f}) <= kDeltaAlphaTreshold) {
    return evaluate_delta(data, scene);
  }

  Frame frame;
  if (data.check_side(frame) == false) {
    return {data.spectrum_sample.wavelength, 0.0f};
  }

  float n_dot_o = dot(frame.nrm, data.w_o);
  float n_dot_i = -dot(frame.nrm, data.w_i);

  float3 m = normalize(data.w_o - data.w_i);
  float m_dot_o = dot(m, data.w_o);

  if ((n_dot_o <= kEpsilon) || (n_dot_i <= kEpsilon) || (m_dot_o <= kEpsilon)) {
    return {data.spectrum_sample.wavelength, 0.0f};
  }

  auto eta_e = data.material.ext_ior(data.spectrum_sample).eta.monochromatic();
  auto eta_i = data.material.int_ior(data.spectrum_sample).eta.monochromatic();
  auto f = fresnel::dielectric(data.spectrum_sample, data.w_i, m, eta_e, eta_i);

  auto ggx = bsdf::NormalDistribution(frame, data.material.roughness);
  auto eval = ggx.evaluate(m, data.w_i, data.w_o);
  float j = 1.0f / (4.0f * m_dot_o);

  auto diffuse = bsdf::apply_image(data.spectrum_sample, data.material.diffuse(data.spectrum_sample), data.material.diffuse_image_index, data.tex, scene);

  BSDFEval result;
  result.func = diffuse * (kInvPi * (1.0f - f)) + data.material.specular(data.spectrum_sample) * (f * eval.ndf * eval.visibility / (4.0f * n_dot_i * n_dot_o));
  ETX_VALIDATE(result.func);
  result.bsdf = diffuse * (kInvPi * n_dot_o * (1.0f - f)) + data.material.specular(data.spectrum_sample) * (f * eval.ndf * eval.visibility / (4.0f * n_dot_i));
  ETX_VALIDATE(result.bsdf);
  result.pdf = kInvPi * n_dot_o * (1.0f - f.monochromatic()) + eval.pdf * j * f.monochromatic();
  ETX_VALIDATE(result.pdf);
  result.weight = result.bsdf / result.pdf;
  ETX_VALIDATE(result.weight);
  return result;
}

ETX_GPU_CODE float pdf(const BSDFData& data, const Scene& scene) {
  if (dot(data.material.roughness, float2{0.5f, 0.5f}) <= kDeltaAlphaTreshold) {
    return pdf_delta(data, scene);
  }

  Frame frame;
  if (data.check_side(frame) == false) {
    return 0.0f;
  }

  float3 m = normalize(data.w_o - data.w_i);
  float m_dot_o = dot(m, data.w_o);
  float n_dot_o = dot(frame.nrm, data.w_o);

  if ((n_dot_o <= kEpsilon) || (m_dot_o <= kEpsilon)) {
    return 0.0f;
  }

  auto eta_e = data.material.ext_ior(data.spectrum_sample).eta.monochromatic();
  auto eta_i = data.material.int_ior(data.spectrum_sample).eta.monochromatic();
  auto f = fresnel::dielectric(data.spectrum_sample, data.w_i, m, eta_e, eta_i);

  auto ggx = bsdf::NormalDistribution(frame, data.material.roughness);

  float j = 1.0f / (4.0f * m_dot_o);
  float result = kInvPi * n_dot_o * (1.0f - f.monochromatic()) + ggx.pdf(m, data.w_i, data.w_o) * j * f.monochromatic();
  ETX_VALIDATE(result);
  return result;
}

ETX_GPU_CODE bool continue_tracing(const Material& material, const float2& tex, const Scene& scene, struct Sampler& smp) {
  if (material.diffuse_image_index == kInvalidIndex) {
    return false;
  }
  const auto& img = scene.images[material.diffuse_image_index];
  return (img.options & Image::HasAlphaChannel) && img.evaluate(tex).w < smp.next();
}

}  // namespace PlasticBSDF

namespace CoatingBSDF {

ETX_GPU_CODE float2 remap_alpha(float2 a) {
  return sqr(max(a, float2{1.0f / 16.0f, 1.0f / 16.0f}));
}

ETX_GPU_CODE BSDFSample sample(Sampler& smp, const BSDFData& data, const Scene& scene) {
  Frame frame;
  if (data.check_side(frame) == false) {
    return {{data.spectrum_sample.wavelength, 0.0f}};
  }

  uint32_t properties = 0;
  BSDFData eval_data = data;
  if (smp.next() <= 0.5f) {
    auto ggx = bsdf::NormalDistribution(frame, remap_alpha(data.material.roughness));
    auto m = ggx.sample(smp, data.w_i);
    eval_data.w_o = normalize(reflect(data.w_i, m));
  } else {
    eval_data.w_o = sample_cosine_distribution(smp.next(), smp.next(), frame.nrm, 1.0f);
    properties = BSDFSample::Diffuse;
  }

  return {eval_data.w_o, evaluate(eval_data, scene), properties};
}

ETX_GPU_CODE BSDFEval evaluate(const BSDFData& data, const Scene& scene) {
  Frame frame;
  if (data.check_side(frame) == false) {
    return {data.spectrum_sample.wavelength, 0.0f};
  }

  auto pow5 = [](float value) {
    return sqr(value) * sqr(value) * fabsf(value);
  };

  float n_dot_o = dot(frame.nrm, data.w_o);
  float n_dot_i = -dot(frame.nrm, data.w_i);

  float3 m = normalize(data.w_o - data.w_i);
  float m_dot_o = dot(m, data.w_o);

  if ((n_dot_o <= kEpsilon) || (n_dot_i <= kEpsilon) || (m_dot_o <= kEpsilon)) {
    return {data.spectrum_sample.wavelength, 0.0f};
  }

  auto eta_e = data.material.ext_ior(data.spectrum_sample).eta.monochromatic();
  auto eta_i = data.material.int_ior(data.spectrum_sample).eta.monochromatic();
  auto f = fresnel::dielectric(data.spectrum_sample, data.w_i, m, eta_e, eta_i);

  auto ggx = bsdf::NormalDistribution(frame, remap_alpha(data.material.roughness));
  auto eval = ggx.evaluate(m, data.w_i, data.w_o);

  auto specular_value = bsdf::apply_image(data.spectrum_sample, data.material.specular(data.spectrum_sample), data.material.specular_image_index, data.tex, scene);
  auto diffuse_value = bsdf::apply_image(data.spectrum_sample, data.material.diffuse(data.spectrum_sample), data.material.diffuse_image_index, data.tex, scene);
  auto fresnel = specular_value + f * (1.0f - specular_value);

  auto diffuse_factor = (28.f / (23.f * kPi)) * (1.0 - specular_value) * (1.0f - pow5(1.0f - 0.5f * n_dot_i)) * (1.0f - pow5(1.0f - 0.5f * n_dot_o));
  auto specular = fresnel * eval.ndf / (4.0f * m_dot_o * m_dot_o);

  BSDFEval result;
  result.func = diffuse_value * diffuse_factor + specular;
  ETX_VALIDATE(result.func);
  result.bsdf = diffuse_value * diffuse_factor * n_dot_o + specular * n_dot_o;
  ETX_VALIDATE(result.bsdf);
  result.pdf = 0.5f * (kInvPi * n_dot_o + eval.pdf / (4.0f * m_dot_o));
  ETX_VALIDATE(result.pdf);
  result.weight = result.bsdf / result.pdf;
  ETX_VALIDATE(result.weight);
  return result;
}

ETX_GPU_CODE float pdf(const BSDFData& data, const Scene& scene) {
  Frame frame;
  if (data.check_side(frame) == false) {
    return 0.0f;
  }

  float3 m = normalize(data.w_o - data.w_i);
  float m_dot_o = dot(m, data.w_o);
  float n_dot_o = dot(frame.nrm, data.w_o);

  if ((n_dot_o <= kEpsilon) || (m_dot_o <= kEpsilon)) {
    return 0.0f;
  }

  auto ggx = bsdf::NormalDistribution(frame, remap_alpha(data.material.roughness));
  float result = 0.5f * (kInvPi * n_dot_o + ggx.pdf(m, data.w_i, data.w_o) / (4.0f * m_dot_o));
  ETX_VALIDATE(result);
  return result;
}

ETX_GPU_CODE bool continue_tracing(const Material& material, const float2& tex, const Scene& scene, struct Sampler& smp) {
  if (material.diffuse_image_index == kInvalidIndex) {
    return false;
  }

  const auto& img = scene.images[material.diffuse_image_index];
  return (img.options & Image::HasAlphaChannel) && img.evaluate(tex).w < smp.next();
}
}  // namespace CoatingBSDF

}  // namespace etx