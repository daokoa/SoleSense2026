# SoleSense Research Basis

Peer-reviewed sources that justify SoleSense's sampling rate, injury-flag thresholds, FSR sensor placement, and the commercial benchmark we compare against.

## Sampling rate

The firmware samples at 500 Hz internally. The lower bounds in the literature target gait analysis; we go higher because impact rising edges during running are 5-20 ms wide and need oversampling for honest loading-rate math.

- **Antonsson & Mann (1985)** -- *Journal of Biomechanics.* GRF signal bandwidth tops out at ~20 Hz, so the Nyquist minimum is 40 Hz.
- **Winter (2009)** -- *Biomechanics of Human Movement.* 50 Hz is the standard clinical rate for gait analysis.
- **Rosenbaum & Becker (1997)** -- *Clinical Biomechanics.* Above ~50 Hz, captured signal includes material vibration and footwear noise, not biomechanics. Our 500 Hz is chosen for impact-edge resolution and is low-pass-filtered in analysis; raw rate is not the same as analysis bandwidth.

## Injury-flag thresholds

The injury flags surfaced by the report screen use the following research-backed cutoffs.

| Flag | Cutoff | Source |
|---|---|---|
| Low cadence | < 160 spm | Heiderscheit et al. (2011), *Medicine & Science in Sports & Exercise* |
| Long ground contact time | > 300 ms | Heiderscheit et al. (2011) |
| Heel striking + high impact rate | heel/forefoot load ratio > 65 %, heel-ball delta > 10 | Lieberman et al. (2010), *Nature* |
| Overpronation | gyro_x running mean > 15 deg/s | Souza (2016), *Journal of Orthopaedic & Sports Physical Therapy* |
| Supination | gyro_x running mean < -8 deg/s | Souza (2016) |
| Medial / lateral asymmetry | > 10 % difference | Zifchock et al. (2006), *Clinical Biomechanics* |

## FSR sensor placement

The 6-sensor layout (3-zone medial/lateral: 2 heel + 2 midfoot + 2 forefoot) is adapted from validated configurations in the literature.

- **Choi et al. (2024)** -- *Sensors (MDPI)*, "Calibrating Low-Cost Smart Insole Sensors with Recurrent Neural Networks for Accurate Prediction of Center of Pressure." Six-FSR insole validated against the Tekscan F-Scan (~$20K commercial system). Feeding the FSR data through an RNN/LSTM improves GRF/CoP prediction accuracy by 30%+. SoleSense's layout matches their validated zone configuration, with sensor E moved next to F so both sit at the heel.
- **Claverie et al. (2016)** -- *Medical Engineering & Physics.* Discrete-sensor distribution strategies for plantar-pressure analysis.

## Height-based stride normalization

Users provide their height at signup. The report uses it to derive an estimated stride length and an estimated speed; the AI Coach receives it in the payload so its analysis can be calibrated to the runner's stature.

- **Cavanagh & Williams (1982)** -- *Medicine & Science in Sports & Exercise* 14(1):30, "The effect of stride length variation on oxygen uptake during distance running." Empirically, **distance-running stride length averages ~0.42 × standing height** at freely-chosen pace, with the natural range 0.40-0.45 × height. SoleSense estimates stride from this factor; the displayed value should be read as *what's typical for a runner your height*, not a measurement.
- **Bramble & Lieberman (2004)** -- *Nature* 432:345, "Endurance running and the evolution of *Homo*." Locomotor parameters (stride length, contact time, vertical oscillation) scale with leg length, which is approximately 0.53 × standing height in adults. Provides the theoretical basis for height-normalizing biomechanical comparisons across runners of different sizes.
- **Cavagna, Franzetti, Heglund & Willems (1988)** -- *Journal of Physiology* 399:81, "The determinants of the step frequency in running, trotting and hopping in man and other vertebrates." Step frequency at a given speed scales with leg length, so shorter runners run at higher cadence than taller runners at identical pace. The 170-180 spm "healthy cadence" guideline applies to adults of typical height (~170-180 cm); shorter or taller runners can be expected to sit slightly above or below that band naturally.
- **Hreljac & Marshall (2000)** -- *Journal of Biomechanics* 33:783, "Algorithms to determine event timing during normal walking using kinematic data." Contact-time interpretation also depends on leg length; the >300 ms "long GCT" threshold is most meaningful when read alongside the runner's stature.

**How SoleSense uses height today:**

1. **Estimated stride length** (`m-stride` on the report): `0.42 × height_m`, per Cavanagh & Williams (1982).
2. **Estimated running speed** (`m-speed` on the report): `cadence_spm × stride_m / 60`, expressed in km/h. Approximate for steady-pace running.
3. **AI Coach payload** (`user.height_cm`): the Cloudflare Worker forwards height to the LLM so its analysis can note things like "your cadence is high *for your height*" instead of treating 170 spm as universal.

**What height is *not* used for** (deliberate — needs more rigorous validation first):

- Adjusting the injury-flag thresholds themselves (cadence < 160 spm, GCT > 300 ms). These remain the universal research-backed cutoffs; the AI Coach can contextualize them by stature but the rule-based flags don't shift.

## Commercial benchmarks

What the SoleSense system is being compared against in the price-vs-fidelity tradeoff.

| Device | Sample rate | Notes |
|---|---|---|
| Pedar-X (Novel GmbH) | 100 Hz internal, 50 Hz output | Lab-grade reference |
| Moticon OpenGO | 100 Hz internal, 25 Hz logged | Consumer wearable |
| Tekscan F-Scan | 100 Hz | Clinical reference, ~$20,000 |
