# Bati et al. 2023 — Detailed Section Extraction Report

**Paper**: "Coupling conduction, convection and radiative transfer in a single path-space: application to infrared rendering"  
**Venue**: ACM Transactions on Graphics (SIGGRAPH 2023), Vol. 42, No. 4  
**Authors**: Bati, Blanco, Coustet, Eymet, Forest, Fournier, Gautrais, Mellado, Paulin, Piaud  
**Page count**: 21 pages (including appendices and references)

---

## 1. Null-collision / Picard Iteration (Section 6.3)

### 1.1 Overall Structure

Section 6.3 "Coupling via the kinetic coefficient" is a **Perspectives** section (not part of the core algorithm), organized into three progressive situations:

| Situation | Section | Topic | Status in Bati 2023 |
|-----------|---------|-------|---------------------|
| 1 | §6.3.1 | Prescribed heterogeneous κ-fields (uncoupled) | Conceptual framework only |
| 2 | §6.3.2 | Coupling with independent physics via κ | Formal derivation (Eq. 38-40) |
| 3 | §6.3.3 | Nonlinear kinetics (κ depends on η itself) | Conceptual + references to Terrée 2022 |

**Key finding**: Bati does NOT implement null-collision in the core algorithm (Sec. 4). The core algorithm uses the **linearized** h_R model where probabilities sum to 1 by construction. Null-collision is proposed as a **future direction** to remove linearization (constraint 5).

### 1.2 How Null-collision Works (§6.3.1–6.3.2)

Starting from the kinetic equation:
$$T[\eta] = -\kappa\eta + \kappa\eta^*$$

**Standard null-collision** (§6.3.1): When κ is heterogeneous but prescribed, set an upper bound $\hat{\kappa}$ so the field becomes homogeneous. At each collision, accept with probability $\kappa/\hat{\kappa}$ (true collision) or continue unchanged (null collision).

**Coupling via κ** (§6.3.2): When κ is unknown (solution of another equation), the null-collision framework is extended with Eq. 39:

> _"At this stage, all we have is more extinction (the kinetic coefficient is replaced with its upper bound) and the surplus in extinction is compensated by the fact that the new source (inside the parenthesis) is the sum of the old source and the sought value of η (a recursion), weighted by the true-collision probability κ/ŵ_κ and its complementary null-collision probability."_ (p.16)

The acceptance probability becomes **random**:

$$P = W_{\kappa,t} / \hat{w}_\kappa$$

> _"true-collision and null-collision probabilities are random: before deciding the collision type, a path is sampled corresponding to the physics of κ, as if estimating κ alone, the resulting Monte Carlo weight is used in place of κ to compute the collision-type probabilities"_ (p.16-17)

### 1.3 Null-collision vs Picard-1

**Critical distinction**: In Bati 2023's core algorithm (Section 4):
- h_R is **linearized** and treated as a prescribed constant: $h_R = 4\sigma\epsilon\theta_{ref}^3$
- This means P_cond + P_conv + P_rad = 1 **exactly** by construction (Eq. 22)
- **No null-collision is needed for Picard-1** — the probabilities are well-defined

**Null-collision becomes necessary only when**:
- The linearization assumption is removed (constraint 5)
- h_R depends on the actual local temperature θ (which is unknown)
- Then h_R = 4σεθ³ involves the unknown θ, making probabilities unknown
- This is "Situation 3: nonlinear kinetics" (§6.3.3)

### 1.4 h_R Linearization Mechanism

From page 7 (Eq. 20):

$$\varphi_{cond} = h(\theta_S - \theta_F) + \int_{2\pi} h_R(\theta_S - \theta_R) \frac{\omega \cdot n}{\pi} d\omega$$

where $h_R = 4\sigma\epsilon\theta_{ref}^3$.

**How it works**:
1. Stefan-Boltzmann: $L = \sigma\theta^4/\pi$
2. Use 1st-order Taylor expansion around θ_ref: $\sigma\theta_S^4 - \sigma\theta_R^4 \approx 4\sigma\theta_{ref}^3(\theta_S - \theta_R)$
3. This defines the **radiative exchange coefficient** $h_R = 4\sigma\epsilon\theta_{ref}^3$
4. h_R is then a constant (given θ_ref), making P_rad well-defined

**T_ref impact**: θ_ref is chosen as a representative scene temperature. The linearization is accurate when all temperatures are "close" to θ_ref. Large temperature gradients make this approximation poor.

> _"[constraint 5]: Linearization assumptions have been made (e.g. radiative exchange coefficient h_R assumed independent of temperature)"_ (p.14)

### 1.5 Nonlinear Coupling / Branching Paths (§6.3.3)

For nonlinear kinetics (removing linearization):

> _"A nonlinear kinetic model is therefore translated into a branching-path statistics."_ (p.17)

> _"such branching paths techniques open avenues to alleviate constraint (5), so as to consider the effect of wider temperature ranges upon radiative transfer, i.e. to take into account situations where the approximation considering h_R as independent of the temperature does not hold."_ (p.17)

This is the theoretical basis for Stardis's PicardN implementation (which the thesis implements as the SFN sub-module).

---

## 2. Simplifying Assumptions

### 2.1 Bati's Five Explicit Constraints (p.14)

Listed verbatim from the paper:

> 1. Solids are opaque and fluid are transparent
> 2. Fluid cells are perfectly mixed
> 3. Properties are uniform and constant in each sub-volume
> 4. All parameters fields are prescribed
> 5. Linearization assumptions have been made (e.g. radiative exchange coefficient h_R assumed independent of temperature)

### 2.2 Detailed Assumption Analysis

| Assumption | Bati Section | Detail |
|-----------|-------------|--------|
| **Opaque solids** | §3.4 | "no radiation inside the solids, only conduction" |
| **Transparent fluids** | §3.4 | "neither diffusion nor absorption/emission in the fluid" → straight-line propagation |
| **Gray body** | §3.4 | "emissivity ε is independent of direction and wavelength (gray)" |
| **Diffuse/specular reflection** | §3.4 | "reflection can be either diffusive (cosine distributed) or specular (or a mix of both)" |
| **Perfectly mixed fluids** | §3.4 | "All connected fluid cells are assumed isothermal except for thin boundary layers" |
| **Uniform properties** | §3.4 | "Solids are rigid and are divided into sub-parts where conductivity and specific heat capacity are uniform" |
| **Prescribed parameters** | Constraint 4 | All h, k, ε, etc. are given, not computed |
| **Linearized radiation** | §4.1 | h_R = 4σεθ³_ref, independent of actual temperature |

### 2.3 Participating Media

**Bati provides the full RTE** (Eq. 1) with volume absorption/scattering in §3.1, but explicitly simplifies to transparent fluids in §3.4:

> _"Radiative transfer reduces therefore to straight line propagation across fluids"_ (p.5)

In §6.2, Bati discusses how participating media can be added back:

> _"had we considered volume radiation, we would still have been dealing with kinetic equations because the Radiative Transfer Equation is kinetic by nature. This alleviates constraint (1)"_ (p.15)

**Conclusion**: Bati presents the general RTE but does NOT implement participating media. Extension is described as straightforward via source coupling.

### 2.4 Non-gray Bodies

Bati's simplified model is **gray** (ε independent of wavelength). The general model (Eq. 1-2) is spectral ($L_\lambda$), but the simplified version integrates over all frequencies:

> _"Integrating over all frequencies, in terms of L = ∫₀^∞ L_λ dλ"_ (p.5)

The Stefan-Boltzmann integration $\int_0^\infty \pi L_{eq}^\lambda(\theta_S) d\lambda = \sigma\theta_S^4$ is used (Eq. 6).

**Non-gray extension**: Not discussed explicitly. Would require spectral sampling and wavelength-dependent ε.

---

## 3. BRDF / Surface Reflection Model

### 3.1 Full Model (§3.1, Eq. 2)

The Rendering Equation uses a general surface phase function $p_r(\omega|\omega_i)$:

$$L_\lambda = \epsilon L_{eq}^\lambda(\theta) + (1-\epsilon) \int_{2\pi} p_r(\omega|\omega_i) L_{\lambda,i} d\omega_i$$

$p_r$ is described as a general BRDF-like term. In the related works (§2, p.3), Bati extensively references BRDF literature:

> _"The transfer on surfaces is modeled using Bidirectional Reflectance Distribution Functions (BRDF), e.g. modeling the micro-geometry of rough surfaces [Bitterli and d'Eon 2022; Wang et al. 2022] or diffractive ones [Holzschuch and Pacanowski 2017], glint paints [Chermain et al. 2020], or volumic micro-structures"_ (p.3)

### 3.2 Simplified Model (§3.4)

For the implementation, the BRDF is simplified to:

> _"reflection can be either diffusive (cosine distributed) or specular (or a mix of both)"_ (p.5)

From the implementation details (p.10):

> _"The surfacic phase function p_r is composed of a specular part and a Lambertian part, sampled accordingly to the specular fraction parameter s (the probability that reflection is specular otherwise diffuse)."_ (p.10)

### 3.3 Thermal Context

In thermal radiation, the BRDF plays a different role than in visible rendering:
- Kirchhoff's law ties emissivity to absorptivity: $\alpha = \epsilon$ (for gray bodies)
- The $(1-\epsilon)$ factor in Eq. 6 represents reflection
- Russian roulette with probability $\epsilon$ determines absorption (path enters BND) vs reflection (path continues RAD)

**Bati does NOT introduce a novel BRDF model for thermal context** — it uses the standard rendering equation formulation, noting that for thermal purposes the gray Lambertian+specular model is sufficient for most engineering scenarios.

---

## 4. Importance Sampling

### 4.1 Explicit Statement on Importance Sampling

From Section 7 (Future Works), p.17:

> _"In our proposal, we did not consider importance sampling nor next event estimation but we pointed-out convergence difficulties associated with images of a well isolated house, where paths starting from an outside camera and reaching the heating system were scarce."_ (p.17)

**Conclusion**: Bati explicitly states NO importance sampling is used.

### 4.2 Cosine-weighted Direction Sampling

Despite the above, **cosine-weighted hemisphere sampling IS used** for the radiation branch at BND interfaces. From Algorithm 4 (p.21):

> `ω ← cosine weighted sampling around n;`  
> `return θ_R(x, ω);   /* Algo. 2 */`

This is a direct consequence of the integral formulation (Eq. 22-23):
$$P_{rad} \int_{2\pi} \theta_R \frac{\omega \cdot n}{\pi} d\omega$$

The $\frac{\omega \cdot n}{\pi}$ factor in the integral is exactly the cosine-weighted PDF, so sampling $\omega$ with cosine weighting cancels the PDF and gives unit weight. This is standard importance sampling of the cosine term.

### 4.3 Three-mode Probabilities as Importance Sampling?

The three-mode branching (P_cond, P_conv, P_rad) in Eq. 22-23 is **technically a form of importance sampling** of the discrete mixture:

$$\theta_S(x) = P_{cond}\theta_S(x-\delta n) + P_{conv}\theta_F + P_{rad}\int_{2\pi}\theta_R\frac{\omega \cdot n}{\pi}d\omega$$

Each P_i is the exact weight for its branch, so selecting branch i with probability P_i and returning the corresponding θ value gives an unbiased estimator with **unit weight** (no importance weight correction needed). This is the ideal importance sampling where the sampling PDF matches the integrand weights exactly.

However, Bati describes this as **"double randomization"** rather than importance sampling:

> _"Reporting Equation 19 into Equation 23 we can use double randomization: expectation of an expectation is an expectation"_ (p.7)

The probabilities are derived from the physics (flux balance), not chosen as a variance-reduction strategy. They are the natural probability weights of the discrete random variable.

---

## 5. Estimator / Path Weight / Termination

### 5.1 Path Termination

A path terminates in exactly **two ways** (p.9):

> _"The path traverses the system backward in time, switching from one heat transfer mode to the other, until either the initial time is reached (initial condition) or it crosses a location where the temperature is known (imposed temperature). These are the two only ways for a path to find its end"_ (p.10)

Specifically:
1. **Known temperature boundary condition** (e.g., thermostat, external environment prescribed temperature)
2. **Initial time reached** (transient case: path rewinds past t_I → return θ(t_I))

### 5.2 Final Estimator Formula

The core estimator is (Eq. 19):

$$\theta_R = E[\theta_S(x_\Gamma)]$$

The radiance temperature at the camera pixel equals the **expectation of the temperature at the path endpoint**.

For the full radiance (before linearization, Eq. 14):

$$L = E\left[\frac{\sigma\theta_S^4(x_\Gamma)}{\pi}\right]$$

### 5.3 Path Weight Accumulation

**The Monte Carlo weight is always 1** — there is no weight accumulation across events. This is because:
- At each branching point, the path selects a branch with the exact physical probability
- The cosine-weighted sampling matches the integral kernel exactly
- Russian roulette absorption (probability ε) exactly matches the physics

The only thing carried to the end is the **temperature value** at the terminal location. No multiplicative weights accumulate.

> _"the Monte Carlo weight is always the value of the temperature at the end of the path"_ (p.10)

This is a remarkable property of the formulation — the entire coupled path has **unit weight**.

### 5.4 Result Type

The estimator returns **temperature** (specifically, radiance temperature θ_R). To get radiance:

$$L = \frac{\sigma\theta_R^4}{\pi}$$

To get a thermal image, the pixel value is the average θ_R over n paths, then converted via Planck function.

---

## 6. Recursive Solver Structure (DFS Algorithm)

### 6.1 Core Algorithm Structure

The algorithm is decomposed into **4 mutually recursive sub-routines** (Appendix B, p.20-21):

| Algorithm | Input | Function |
|-----------|-------|----------|
| **Algo. 1**: Convective sub-path | time t | Sample τ_conv, rewind time, return θ_F(t_I) or θ_B(x_S) via Algo. 4 |
| **Algo. 2**: Radiative sub-path | position x, direction ω | Ray trace, Russian roulette (absorb → Algo. 4, reflect → sample new ω, loop) |
| **Algo. 3**: Conductive sub-path | position x, time t | Isotropic direction, δ-sphere walk, rewind time, boundary → Algo. 4, else loop |
| **Algo. 4**: Boundary chaining | position x | Identify interface type (SS/SF), compute probabilities, dispatch to Algo. 1/2/3 |

### 6.2 Four-mode Dispatch (Algorithm 4)

```
if x is on solid-solid interface:
    P_cond,1 = (k1/δ1) / (k1/δ1 + k2/δ2)
    random select side → Algo. 3 (conduction)

else if x is on solid-fluid interface:
    P_conv = h / (k/δ + h + h_R)
    P_rad  = h_R / (k/δ + h + h_R)
    P_cond = 1 - P_conv - P_rad
    
    random < P_conv → Algo. 1 (convection, return θ_F)
    random < P_conv + P_cond → Algo. 3 (conduction, return θ_S(x-δn))
    else → sample ω cosine-weighted, Algo. 2 (radiation, return θ_R(x,ω))
```

### 6.3 Where Recursion Happens

Recursion occurs at **every boundary event** (Algorithm 4):
- **RAD → BND**: Absorption event (Russian roulette ε) triggers Algo. 4
- **BND → CND**: P_cond branch triggers Algo. 3
- **BND → CNV**: P_conv branch triggers Algo. 1
- **BND → RAD**: P_rad branch triggers Algo. 2
- **CND → BND**: Walk-on-δ-sphere reaches boundary, triggers Algo. 4
- **CNV → BND**: Convective sub-path samples surface location, triggers Algo. 4

The recursion is **tail-recursive** in structure — each call returns a temperature, and the caller uses it directly as its own return value. No multiplicative weight accumulation occurs.

### 6.4 Algorithm Pseudocode (verbatim from Appendix B)

**Algorithm 2: Radiative sub-path**
```
Input: position x, direction ω
Output: temperature θ
Loop:
    s ← Trace ray from x along ω
    if s = ∞: return θ_R,ambient
    x ← x + s·ω
    r ← uniform [0,1)
    if r < ε(x): return θ_B(x)     // Algo. 4
    else: ω ← Sample direction wrt p_r at x
EndLoop
```

**Algorithm 3: Conductive sub-path**
```
Input: position x, time t
Output: temperature θ
Loop:
    ω ← isotropic sampling
    s+ ← Trace ray from x along +ω
    s- ← Trace ray from x along -ω
    δ̃ ← min(min(δ,s+), min(δ,s-))
    τ_cond ← ρcδ̃²/(6k)
    T_b,cond ← exponential sampling with mean τ_cond
    t ← t - T_b,cond                // Time rewind
    if t ≤ t_I: return θ_S(t_I)     // Initial temperature
    x ← x + δ̃·ω
    if x is on boundary: return θ_B(x)  // Algo. 4
EndLoop
```

---

## Summary Table: Bati 2023 vs Thesis Implementation

| Feature | Bati 2023 (Core Algorithm) | Bati 2023 (Perspectives §6) | Thesis Implementation |
|---------|---------------------------|----------------------------|-----------------------|
| **Null-collision at BND** | Not used (h_R linearized) | Proposed for nonlinear coupling | Implemented (SF null-collision loop) |
| **PicardN branching** | Not implemented | Described conceptually (§6.3.3) | Implemented (SFN sub-module) |
| **Participating media** | Full RTE given but simplified out | Extension described as straightforward | Not implemented (same as Bati core) |
| **Non-gray surfaces** | Spectral RTE given but simplified to gray | Not discussed | Not implemented (same as Bati core) |
| **BRDF model** | Lambert + specular mix | General p_r in formulation | Lambert + specular mix |
| **Importance sampling** | Explicitly not used (stated in §7) | Identified as future work | Not used |
| **Cosine-weighted sampling** | Yes (Algorithm 4, RAD branch) | — | Yes |
| **Path weight** | Always = 1 (unit weight) | — | Unit weight (same) |
| **Result type** | Temperature θ | — | Temperature θ |
| **Solver structure** | Recursive DFS (4 mutually recursive algorithms) | — | FSM Wavefront (flattened DFS) |
| **ENC (enclosure query)** | Not mentioned (implicit in Stardis) | — | Explicit 6-ray voting sub-module |
| **EXT (external environment)** | θ_R,ambient (single value) | — | Full solar+sky environment model |

---

*Extracted: 2026-04-05*  
*Source PDF: `C:\Users\77978\Zotero\storage\9Z74QPAZ\Bati 等 - 2023 - ...pdf`*
