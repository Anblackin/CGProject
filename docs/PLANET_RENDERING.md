# Planet appearance and DIY controls

The visual direction is natural space photography: a directional key, readable terrain at grazing angles, dark nights and a thin illuminated atmosphere. This is an interactive artistic model, with the same fixed Sun and eight-planet catalog and the existing CPU rasterizer.

## Surface and lighting

Displaced meshes keep their geometric normals. A bounded, once-per-body/layer cache stores the height residual above the mesh frequency and its derivatives. The fragment stage adds only that fine detail, using the spherical metric and a polar fade. Detail is omitted on discs smaller than 20 projected pixels in radius, avoiding cache initialization for tiny overview bodies. Earth and Mars retain their relative height images; other surfaces and gas cloud tops retain their image-derived artistic heights. `surface_detail` controls fine shading without changing the silhouette or sculpt coordinates.

The renderer selects the light from the actual snapshot: if it contains a Sun, planets point toward it. Isolated design snapshots use a consistent photographic key. Display lighting has no inverse-square exposure falloff, so outer planets remain visible at the diagram's illustrative distances. Earth's blue-water mask has a Fresnel-weighted, narrow highlight; land uses rough diffuse shading with a weak surface reflection.

## Rocky surface finish

Mercury, Mars, Earth's land and Venus's exposed radar surface use a bounded rough diffuse response inspired by Oren–Nayar and weak dielectric highlights. `surface_roughness` ranges from 0 to 1 with default 0.7; at zero the diffuse term is Lambertian. Wider rough highlights remain subdued. The existing geometric and fine-detail normals provide slopes and crater cues; this change adds no new elevation or craters. Earth's added polar ice mask blends toward a smoother response and suppresses water highlights under that added ice. Fully classified ocean regions retain their prior response. Sun, gas planets, rings and Venus's visible cloud deck retain their existing shading.

The optional project field is validated and saved with the other design overrides, so history and draft recovery include it. These materials are artistic display approximations with bounded lighting terms, not measured BRDFs or a spectral solver. No additional per-fragment height samples or shadow rays are introduced.

## Clouds and atmosphere

Earth's independent cloud shell sits above a conservative bound on the terrain and positive sculpt displacement. `cloud_altitude` is additional clearance as a fraction of the body radius. Clouds use a drifting texture, latitude-corrected mask slopes with a polar fade, and greater optical depth near the limb. A bounded optical-depth response differentiates thin filaments and dense cloud tops; a weak forward-scattering cue is gated by the radial day/night boundary so fine lighting detail cannot light the deep night side. Cloud coverage and the existing view-opacity equation are unchanged.

Surface shadows intersect the same cloud shell and sample the same drifting mask. Direct-light transmission follows a Beer–Lambert approximation with vertical optical depth `2 * opacity * mask` and a grazing cosine floor of 0.15. `cloud_shadow` controls attenuation strength; zero opacity or shadow strength removes this attenuation. Low-angle sunlight therefore travels through a larger optical column. The density mask and optical-depth scale are artistic; this does not solve weather, cloud-volume self-shadowing or terrain shadow rays. No new texture taps or saved controls are added by this refinement.

Thin atmospheric shells use a short exponential scale height, four integration steps, a daylight/twilight weight and a Rayleigh-inspired phase term. Density fades to zero at the outer boundary. Sample depth clips the integration against visible foreground geometry. Scattering changes sample colors without changing depth, body ownership or sculpt UV. Pure halo samples report `Atmosphere` in the pixel trace; they do not count as geometry edges in AA measurements. Venus integrates rim scattering in its visible cloud-deck fragment so that its cloud opacity is applied exactly once and its radar surface stays independent. Mercury has no atmosphere; the Sun remains emissive.

These are inexpensive single-scattering cues rather than a spectral atmosphere solver. The cloud shell and terrain relief are exaggerated for editing visibility. High sculpt peaks may sit above the densest atmospheric layer. There is no bloom, weather simulation or multiple scattering.

## Rings and storms

Saturn's ring geometry, normals and shadow plane share one tilt transform. A bounded plane-parallel single-scattering approximation distinguishes its reflected and transmitted faces using signed light/view cosines. Texture opacity provides a normal-incidence optical-depth proxy; denser bands attenuate transmitted light more strongly, while thin backlit bands remain visible. Equal light/view cosines use an analytic limit, and grazing cosines have a floor of 0.06 with an incident-light fade. The artistic gain and ambient floor are bounded; this is not a measured particle phase function. Visible texture alpha and its 0.03 cutoff are unchanged, so ring coverage and selection keep their existing behavior.

A ray toward the light tests a spherical planet for the shadow on the rings, attenuating direct ring light while preserving a small ambient floor. A ray from the surface to the ring plane samples the actual ring opacity and Cassini gap; the same optical-depth proxy now sets angle-dependent direct-light transmission onto the planet. The body shadow uses the reference sphere, so deeply sculpted silhouettes do not cast matching detailed shadows. This refinement adds arithmetic without new texture taps or shadow rays. Uranus's thin rings retain their artistic material and follow its complete tilted-axis transform. The ring remains a thin surface; its visible alpha is illustrative rather than a calibrated angular-extinction model.

Gas-band warp and storm relocation use one inverse deformation for color, shape and fine normals. A monotone latitude map fixes the poles; a periodic longitude stretch relocates the original storm without creating a second copy. Fine derivatives include this deformation's Jacobian. Large moves and size/aspect changes redistribute neighboring bands; they do not synthesize a new fluid vortex. Sculpt displacement and picking remain at geometric UV coordinates. Defaults leave baseline UV unchanged.

## Saved parameters

| Field | Range | Default |
| --- | --- | --- |
| `surface_detail` | 0–2 | 1 |
| `atmosphere_density` | 0–2 | 1 |
| `cloud_altitude` | 0.005–0.06 | 0.018 |
| `cloud_shadow` | 0–1 | 0.65 |
| `band_warp` | 0–1 | 0 |
| `storm_longitude` | 0–1, one full turn | Jupiter 0.37; Neptune 0.19 |
| `storm_latitude` | 0.08–0.92, north to south | Jupiter 0.61; Neptune 0.16 |
| `storm_aspect` | 0.5–2 | 1 |

Fields are optional when reading older JSON and explicit when saving. Defaults come from the body catalog, including Neptune. The inspector only exposes controls applicable to the selected body. Existing `storm_scale` changes storm size, and `storm_relief` now applies to Neptune as well as Jupiter. The application presents storm longitude and latitude in degrees.

## Reference study

- [threejs-procedural-planets](https://github.com/dgreenheck/threejs-procedural-planets): studied the actual vertex/fragment shaders in `index.html`, `atmosphere.js` and UI parameters. Displaced tangent/bitangent normals and height-masked specular informed this implementation. Its particle atmosphere was not ported.
- [3D-CSS-Solar-System](https://github.com/juliangarnier/3D-CSS-Solar-System): studied its orbital presentation and animated inset shadows. Ring/planet shadows here use analytical geometry rather than the CSS animation.

OpenGL and Metal still display the CPU image. MSAA retains sample-level coverage/depth while reusing fragment and atmospheric shading within a pixel; SSAA shades each sample. The trace contains the final composited linear RGB, and render time and shading counts include the new optical passes. Existing asynchronous, reduced-resolution interaction previews continue to bound the cost while editing.

Scalar texture sampling avoids interpolating unused channels for cloud masks and ring-shadow alpha. RGB means use one interpolation only when all four sampled RGB corners are grayscale, otherwise they retain three-channel averaging. Detection is local to the footprint, so edited or colored textures cannot leave stale grayscale metadata. Bilinear arithmetic, validation, wrapping, cloud cutoffs and ring alpha sampling at latitude 0.5 stay unchanged. This reduces CPU work without altering resolution or optical formulas; six fixed cloud/ring before-and-after scenes retain byte-identical displayed images and body/UV buffers. Timing benefits depend on scene and hardware; this does not guarantee interactive frame rates at larger resolutions.
