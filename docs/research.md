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

## Commercial benchmarks

What the SoleSense system is being compared against in the price-vs-fidelity tradeoff.

| Device | Sample rate | Notes |
|---|---|---|
| Pedar-X (Novel GmbH) | 100 Hz internal, 50 Hz output | Lab-grade reference |
| Moticon OpenGO | 100 Hz internal, 25 Hz logged | Consumer wearable |
| Tekscan F-Scan | 100 Hz | Clinical reference, ~$20,000 |
