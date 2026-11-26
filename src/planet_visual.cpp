#include "planet_sandbox/planet_visual.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace planet_sandbox {
namespace {
constexpr double pi = 3.14159265358979323846;
double wrap(double u) { return u - std::floor(u); }

double stretch_latitude(double v, double center, double factor) {
    const double side = v < center ? center : 1.0 - center;
    const double x = (v - center) / side;
    return center + side * factor * x / (1.0 + (factor - 1.0) * std::abs(x));
}
} // namespace

double ring_light_transmission(double opacity,double light_cosine) {
    const double alpha=std::clamp(opacity,0.0,1.0);
    if(alpha==0) return 1;
    if(alpha==1) return 0;
    const double cosine=std::max(.06,std::min(1.0,std::abs(light_cosine)));
    return std::exp(std::log1p(-alpha)/cosine);
}

double ring_reflectance(double opacity,double light_cosine,double view_cosine,double sunlight_visibility) {
    const double alpha=std::clamp(opacity,0.0,1.0);
    if(alpha==0) return 0;
    const double incident=std::min(1.0,std::abs(light_cosine));
    const double mu_light=std::max(.06,incident),mu_view=std::max(.06,std::min(1.0,std::abs(view_cosine)));
    const double depth=alpha<1?-std::log1p(-alpha):0;
    double scattered=0;
    if(light_cosine*view_cosine>=0) {
        const double extinction=alpha==1?1:-std::expm1(-depth*(1/mu_light+1/mu_view));
        scattered=mu_light/(mu_light+mu_view)*extinction;
    } else if(alpha<1) {
        const double difference=std::abs(mu_light-mu_view);
        if(difference<1e-4*std::max(mu_light,mu_view)) {
            // The equal-angle limit avoids cancellation in transmitted single scattering.
            const double mu=(mu_light+mu_view)*.5;
            scattered=depth/mu*std::exp(-depth/mu);
        } else {
            const double high=std::max(mu_light,mu_view),low=std::min(mu_light,mu_view);
            const double delta=std::exp(-depth/high)*-std::expm1(-depth*(1/low-1/high));
            scattered=mu_light/difference*delta;
        }
    }
    // Texture opacity remains the compositing coverage; normalize source light separately.
    const double direct=std::clamp(1.6*scattered/alpha*(incident/mu_light),0.0,1.0);
    return .025+.975*direct*std::clamp(sunlight_visibility,0.0,1.0);
}

double cloud_light_transmission(double mask,double opacity,double light_cosine) {
    const double depth=std::clamp(opacity,0.0,1.0)*std::clamp(mask,0.0,1.0)*2;
    return std::exp(-depth/std::max(.15,std::clamp(light_cosine,0.0,1.0)));
}

CloudReflectance cloud_reflectance(double mask,double opacity,double radial_light_cosine,
                                   double detail_light_cosine,double view_cosine,double light_view_cosine) {
    const double depth=std::clamp(opacity,0.0,1.0)*std::clamp(mask,0.0,1.0)*2;
    if(depth==0) return {};
    // Preserve the existing shell coverage and its grazing-angle floor exactly.
    const double alpha=1-std::exp(-depth/std::max(.35,view_cosine));
    const double incident=1-cloud_light_transmission(mask,opacity,radial_light_cosine);
    const double twilight=std::clamp((radial_light_cosine+.02)/.10,0.0,1.0);
    const double day=twilight*twilight*(3-2*twilight);
    const double direct=std::clamp(detail_light_cosine,0.0,1.0)*(.65+.35*std::sqrt(incident));
    const double phase=std::clamp(-light_view_cosine,0.0,1.0);
    const double phase2=phase*phase,phase4=phase2*phase2;
    const double forward=.08*incident*phase4*phase4;
    return {alpha,std::clamp(.025+.975*day*(direct+forward),0.0,1.0)};
}

RockReflectance rock_reflectance(VisualVector normal,VisualVector light,VisualVector view,double roughness) {
    const auto dot=[](VisualVector a,VisualVector b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const double nl=std::clamp(dot(normal,light),0.0,1.0),nv=std::clamp(dot(normal,view),0.0,1.0);
    if(nl<=0 || nv<=0) return {};
    const double r=std::clamp(roughness,0.0,1.0),sigma2=r*r*1.21;
    const double a=1-.5*sigma2/(sigma2+.33),b=.45*sigma2/(sigma2+.09);
    const double sl=std::sqrt(std::max(0.0,1-nl*nl)),sv=std::sqrt(std::max(0.0,1-nv*nv));
    const double cosine=sl*sv>1e-8?std::max(0.0,(dot(light,view)-nl*nv)/(sl*sv)):0.0;
    const double low=std::min(nl,nv),high=std::max(nl,nv);
    const double angular=std::sqrt(std::max(0.0,1-low*low))*
        std::sqrt(std::max(0.0,1-high*high))/std::max(.05,high);
    const double diffuse=std::clamp(nl*(a+b*cosine*angular),0.0,1.0);
    const VisualVector half{light.x+view.x,light.y+view.y,light.z+view.z};
    const double length=std::sqrt(dot(half,half));
    if(length<1e-8) return {diffuse,0};
    const VisualVector h{half.x/length,half.y/length,half.z/length};
    const double width=.12+.88*r;
    const double exponent=std::clamp(2/std::pow(width,4)-2,2.0,256.0);
    const double fresnel=.04+.96*std::pow(1-std::clamp(dot(view,h),0.0,1.0),5);
    const double specular=std::clamp((exponent+2)/64*fresnel*
        std::pow(std::max(0.0,dot(normal,h)),exponent)*nl,0.0,.12);
    return {diffuse,specular};
}

RockReflectance rock_surface_reflectance(VisualVector normal,VisualVector light,VisualVector view,
                                         double land_roughness,double ocean_coverage,double ice_coverage) {
    const double ocean=std::clamp(ocean_coverage,0.0,1.0),ice=std::clamp(ice_coverage,0.0,1.0);
    const auto rock=rock_reflectance(normal,light,view,land_roughness*(1-ice)+.18*ice);
    const double lambert=std::max(0.0,normal.x*light.x+normal.y*light.y+normal.z*light.z);
    return {lambert*ocean+rock.diffuse*(1-ocean),rock.specular*(1-ocean)*(1+ice)};
}

BodySurfaceRotation body_surface_rotation(BodyKind kind,std::uint32_t seed,double axis_tilt_offset,double time) {
    const double spin=time*.45+(kind==BodyKind::Custom?seed*.017:0.0);
    const double tilt=kind==BodyKind::Uranus?(98+axis_tilt_offset)*pi/180.0:0.0;
    return {std::cos(spin),std::sin(spin),std::cos(tilt),std::sin(tilt),kind==BodyKind::Uranus};
}

VisualVector BodySurfaceRotation::to_world(VisualVector local) const {
    const VisualVector spun{local.x*spin_cos+local.z*spin_sin,local.y,
                           -local.x*spin_sin+local.z*spin_cos};
    const VisualVector tilted{spun.x*tilt_cos-spun.y*tilt_sin,
                             spun.x*tilt_sin+spun.y*tilt_cos,spun.z};
    return uranus?VisualVector{tilted.x*std::cos(.35)+tilted.z*std::sin(.35),tilted.y,
                              -tilted.x*std::sin(.35)+tilted.z*std::cos(.35)}:tilted;
}

VisualVector BodySurfaceRotation::to_local(VisualVector world) const {
    const VisualVector tilted=uranus?VisualVector{world.x*std::cos(.35)-world.z*std::sin(.35),world.y,
                                                  world.x*std::sin(.35)+world.z*std::cos(.35)}:world;
    const VisualVector spun{tilted.x*tilt_cos+tilted.y*tilt_sin,
                           -tilted.x*tilt_sin+tilted.y*tilt_cos,tilted.z};
    return {spun.x*spin_cos-spun.z*spin_sin,spun.y,spun.x*spin_sin+spun.z*spin_cos};
}

Vec2 solar_visual_uv(BodyKind kind, double u, double v, const DesignOverrides& d) {
    u = wrap(u);
    v = std::clamp(v, 0.0, 1.0);
    if (kind != BodyKind::Jupiter && kind != BodyKind::Saturn &&
        kind != BodyKind::Uranus && kind != BodyKind::Neptune) return {u, v};
    if (v == 0.0 || v == 1.0) return {u, v};
    if (kind == BodyKind::Jupiter || kind == BodyKind::Neptune) {
        const double source_u = kind == BodyKind::Jupiter ? 0.37 : 0.19;
        const double source_v = kind == BodyKind::Jupiter ? 0.61 : 0.16;
        if (d.storm_longitude != source_u || d.storm_latitude != source_v ||
            d.storm_aspect != 1.0 || d.storm_scale != 1.0) {
            const double size = std::max(0.15, d.storm_scale);
            const double angle = pi * std::remainder(u - d.storm_longitude, 1.0);
            u = wrap(source_u + std::atan2(std::sin(angle) / (size * std::sqrt(d.storm_aspect)),
                                           std::cos(angle)) / pi);
            // A monotone rational transform fixes both poles and relocates one storm.
            const double a = source_v * (1.0 - d.storm_latitude);
            const double b = d.storm_latitude * (1.0 - source_v);
            v = a * v / (b + (a - b) * v);
            v = stretch_latitude(v, source_v, std::sqrt(d.storm_aspect) / size);
        }
    }
    if (v <= 0.0) return {u, 0.0};
    if (v >= 1.0) return {u, 1.0};
    if (d.band_warp > 0.0) {
        const double envelope = std::sin(pi * v);
        v += d.band_warp * 0.025 * envelope * envelope *
             (std::sin(6.0 * pi * u + 9.0 * v) + 0.35 * std::sin(14.0 * pi * u - 13.0 * v));
    }
    return {u, std::clamp(v, 0.0, 1.0)};
}

double solar_storm_mask(BodyKind kind, double u, double v) {
    if (kind != BodyKind::Jupiter && kind != BodyKind::Neptune) return 0.0;
    const bool jupiter = kind == BodyKind::Jupiter;
    const double du = std::remainder(u - (jupiter ? 0.37 : 0.19), 1.0) / (jupiter ? 0.05 : 0.075);
    const double dv = (v - (jupiter ? 0.61 : 0.16)) / (jupiter ? 0.045 : 0.055);
    return std::exp(-0.5 * (du * du + dv * dv));
}

const VisualDetailMap& solar_detail_map(BodyKind kind, SolarShapeLayer layer) {
    static std::array<VisualDetailMap, 20> cache;
    static std::array<std::once_flag, 20> flags;
    const auto index = static_cast<std::size_t>(kind) * 2 +
                       (layer == SolarShapeLayer::VenusRadar ? 1 : 0);
    auto& map = cache.at(index);
    std::call_once(flags.at(index), [&] {
        map.width = 512; map.height = 256;
        std::vector<double> residual(map.width * map.height);
        const auto height_at = [&](double u, double v) {
            return layer == SolarShapeLayer::VenusRadar ? solar_venus_radar_height_sample(u, v)
                                                       : solar_height_sample(kind, u, v);
        };
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                const double u = double(x) / map.width, v = double(y) / (map.height - 1);
                double low = 0;
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                    low += height_at(u + dx / 96.0, v + dy / 48.0) / 9.0;
                residual[y * map.width + x] = height_at(u, v) - low;
            }
        }
        map.gradient.resize(residual.size());
        for (int y = 0; y < map.height; ++y) for (int x = 0; x < map.width; ++x) {
            map.gradient[y * map.width + x] = {
                (residual[y * map.width + (x + 1) % map.width] -
                 residual[y * map.width + (x + map.width - 1) % map.width]) * map.width * 0.5,
                (residual[std::min(y + 1, map.height - 1) * map.width + x] -
                 residual[std::max(y - 1, 0) * map.width + x]) * (map.height - 1) * 0.5};
        }
    });
    return map;
}

Vec2 sample_visual_detail(const VisualDetailMap& map, double u, double v) {
    const double x = wrap(u) * map.width, y = std::clamp(v, 0.0, 1.0) * (map.height - 1);
    const int x0 = static_cast<int>(x) % map.width, y0 = static_cast<int>(y);
    const int x1 = (x0 + 1) % map.width, y1 = std::min(y0 + 1, map.height - 1);
    const double fx = x - std::floor(x), fy = y - y0;
    const auto a = map.gradient[y0 * map.width + x0], b = map.gradient[y0 * map.width + x1];
    const auto c = map.gradient[y1 * map.width + x0], d = map.gradient[y1 * map.width + x1];
    return {(a.x * (1 - fx) + b.x * fx) * (1 - fy) + (c.x * (1 - fx) + d.x * fx) * fy,
            (a.y * (1 - fx) + b.y * fx) * (1 - fy) + (c.y * (1 - fx) + d.y * fx) * fy};
}

VisualVector perturb_visual_normal(VisualVector normal, VisualVector radial, VisualVector pole,
                                    double longitude_slope, double latitude_slope) {
    const auto cross = [](VisualVector a, VisualVector b) -> VisualVector {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    const auto unit = [](VisualVector p) -> VisualVector {
        const double length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        return length > 1e-12 ? VisualVector{p.x / length, p.y / length, p.z / length} : VisualVector{};
    };
    const auto along = unit(cross(radial, pole));
    const auto down = unit(cross(radial, along));
    return unit({normal.x - along.x * longitude_slope - down.x * latitude_slope,
                 normal.y - along.y * longitude_slope - down.y * latitude_slope,
                 normal.z - along.z * longitude_slope - down.z * latitude_slope});
}

bool planet_blocks_light(VisualVector p, VisualVector l, double radius) {
    const double along = p.x * l.x + p.y * l.y + p.z * l.z;
    const double perpendicular = p.x * p.x + p.y * p.y + p.z * p.z - along * along;
    return along < 0.0 && perpendicular < radius * radius;
}

double ring_plane_radius(VisualVector p, VisualVector l, VisualVector n) {
    const double denominator = l.x * n.x + l.y * n.y + l.z * n.z;
    if (std::abs(denominator) < 1e-8) return -1.0;
    const double t = -(p.x * n.x + p.y * n.y + p.z * n.z) / denominator;
    if (t <= 1e-6) return -1.0;
    p = {p.x + t * l.x, p.y + t * l.y, p.z + t * l.z};
    return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
}

} // namespace planet_sandbox
