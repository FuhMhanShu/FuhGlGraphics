#include "fuhglgraphics/scene/effects.hpp"

#include "fuhglgraphics/render/deferred_renderer.hpp"

namespace fuhglgraphics::scene {

void Effects::setAll(bool on) {
    list.clear();
    if (on) {
        for (int i = 0; i < render::kEffectCount; ++i)
            list.push_back(static_cast<render::Effect>(i));
    }
}

void Effects::applyTo(render::DeferredRenderer& renderer) const {
    renderer.setAllEffects(false);
    for (render::Effect e : list) renderer.setEffectEnabled(e, true);
    renderer.exposure = params.exposure;
    renderer.taaBlend = params.taaBlend;
    renderer.upscaleSharpness = params.upscaleSharpness;
    renderer.setShadowStrength(params.shadowStrength);
    renderer.setShadowSceneBounds(params.shadowCenter, params.shadowRadius);
    renderer.setShadowZRange(params.shadowZNear, params.shadowZFar);
    // F45 shadow-kit parameters
    renderer.setShadowFilter(params.shadowFilter);
    renderer.shadowSnapToTexels = params.shadowSnapToTexels;
    renderer.shadowCascadeBlend = params.shadowCascadeBlend;
    renderer.shadowLightSize = params.shadowLightSize;
    renderer.shadowMinVariance = params.shadowMinVariance;
    renderer.shadowNormalOffset = params.shadowNormalOffset;
    renderer.shadowPcfRadius = params.shadowPcfRadius;
    // G7 light probes
    renderer.lightProbeStrength = params.lightProbeStrength;
    // G10 DoF
    renderer.dofFocusDist = params.dofFocusDist;
    renderer.dofStrength = params.dofStrength;
    renderer.dofMaxCoC = params.dofMaxCoC;
    // G15 SSAO
    renderer.ssaoRadius = params.ssaoRadius;
    renderer.ssaoIntensity = params.ssaoIntensity;
    renderer.ssaoBias = params.ssaoBias;
    renderer.ssaoSamples = params.ssaoSamples;
    // G16 SSR
    renderer.ssrMaxSteps = params.ssrMaxSteps;
    renderer.ssrThickness = params.ssrThickness;
    renderer.ssrMaxDist = params.ssrMaxDist;
    renderer.ssrStrength = params.ssrStrength;
    // G17 bloom
    renderer.bloomThreshold = params.bloomThreshold;
    renderer.bloomStrength = params.bloomStrength;
    renderer.bloomRadius = params.bloomRadius;
    // G21 volumetric fog
    renderer.volumetricDensity = params.volumetricDensity;
    renderer.volumetricCloudiness = params.volumetricCloudiness;
    renderer.volumetricHeightFalloff = params.volumetricHeightFalloff;
    renderer.volumetricNoiseScale = params.volumetricNoiseScale;
    renderer.volumetricAnisotropy = params.volumetricAnisotropy;
    renderer.volumetricMaxDist = params.volumetricMaxDist;
    renderer.volumetricStrength = params.volumetricStrength;
    renderer.volumetricSteps = params.volumetricSteps;
    // G22 wetness
    renderer.wetnessAmount = params.wetnessAmount;
    renderer.wetnessPuddle = params.wetnessPuddle;
    renderer.wetnessNoiseScale = params.wetnessNoiseScale;
    // G31 weathering
    renderer.weatherStrength = params.weatherStrength;
    renderer.weatherHumidity = params.weatherHumidity;
    renderer.weatherBaseY = params.weatherBaseY;
    renderer.weatherRainStreaks = params.weatherRainStreaks;
    renderer.weatherSoilColor = params.weatherSoilColor;
    renderer.weatherMossColor = params.weatherMossColor;
    // G23 tonemap operator
    renderer.tonemapOperator = static_cast<render::TonemapOperator>(params.tonemapOperator);
    // G30 lens flare + anamorphic streaks
    renderer.lensFlareStrength = params.lensFlareStrength;
    renderer.anamorphicStrength = params.anamorphicStrength;
    renderer.anamorphicStretch = params.anamorphicStretch;
    renderer.anamorphicThreshold = params.anamorphicThreshold;
    // G31 HBAO
    renderer.hbaoRadius = params.hbaoRadius;
    renderer.hbaoBias = params.hbaoBias;
    renderer.hbaoDirections = params.hbaoDirections;
    renderer.hbaoSteps = params.hbaoSteps;
    renderer.hbaoPower = params.hbaoPower;
    // G32 chromatic aberration
    renderer.caStrength = params.caStrength;
    // G33 subsurface scattering
    renderer.sssDistortion = params.sssDistortion;
    renderer.sssPower = params.sssPower;
    renderer.sssScale = params.sssScale;
    renderer.sssThickness = params.sssThickness;
    // G34 screen-space GI
    renderer.giStrength = params.giStrength;
    renderer.giCones = params.giCones;
    renderer.giSteps = params.giSteps;
    renderer.giMaxDist = params.giMaxDist;
    renderer.giThickness = params.giThickness;
    // (width() > 0: the renderer was create()d, so a GL context is current and the
    //  cube map can be allocated)
    if (enabled(render::Effect::PointShadows) && !renderer.pointShadowEnabled() && renderer.width() > 0)
        renderer.enablePointShadow(-1, 1024); // first point light of the frame, cube map 1024^2
}

} // namespace fuhglgraphics::scene
