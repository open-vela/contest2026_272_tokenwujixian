# BK7258 DAC Playback Bring-up Spec

> Purpose: a register-level specification of the BK7258 internal DAC playback
> path (SoC + DAC, driving HT6873 PA externally).  It captures every write,
> read-modify-write and delay that has to happen in order to turn a stream of
> S16_LE PCM samples into differential analog output at pins.  The
> implementation in `chips/bk7258/audio/` follows this document; the document
> in turn is reverse-engineered from the Beken AVDK
> (`bk_avdk_smp/ap/middleware/driver/audio/aud/`,
> `bk_avdk_smp/ap/middleware/soc/bk7258_ap/hal/`, and `.../soc/`), all under
> Apache-2.0.
>
> Anything that is **hardware guesswork** (APLL magic constants, per-field
> ana_reg defaults, wait polling loops) is marked ⚠ and MUST be preserved
> byte-for-byte from the AVDK source when we implement it.  Anything that is
> plain glue (fifo threshold, dequeue policy) is fair game to redesign in
> NuttX style.

## 0. SoC configuration flags

`bk_avdk_smp/ap/components/bk_libs/bk7258_ap/config/sdkconfig` defines:

```
CONFIG_SOC_BK7258=y
CONFIG_SOC_BK7236XX=y      # <-- 7258 shares the "36XX" audio code path
# CONFIG_SOC_BK7256XX is not set
```

All AVDK code below is compiled under `CONFIG_SOC_BK7236XX`.  The
`CONFIG_SOC_BK7256XX` alternative branch (with sys_drv_analog_reg12~17 and
aud_vdd1v/1v5) does **not** apply to BK7258 and must be ignored.

## 1. Register bases

All addresses below are AP-visible physical addresses.

| Region                | Base         | Notes                                           |
|-----------------------|--------------|-------------------------------------------------|
| Audio (AUD) block     | `0x47800000` | DAC/ADC/FIFO/DTMF; per-reg offset `r * 4`.      |
| Sys ctrl block        | `0x44010000` | Clock enables, analog reg SPI window.           |
| Analog reg SPI status | `0x440100E8` | `SOC_SYS_REG_BASE + (0x3a << 2)`, poll bit-idx. |

Analog registers `ana_regN` (N ∈ 0..0x2A) are memory-mapped in the sys block
at `SOC_SYS_REG_BASE + ((0x40 + N) << 2)`, e.g. `ana_reg18 = 0x44010148`.
They are backed by an on-chip SPI interface; every write is followed by
polling `SOC_SYS_REG_BASE + (0x3a << 2)` bit `N` until it clears (max latency
is a handful of AHB cycles, no explicit timeout is used by AVDK).

### 1.1 Audio (AUD) register offsets used by playback

| Name                  | Offset | Address       | Purpose                                    |
|-----------------------|--------|---------------|--------------------------------------------|
| `AUD_CLK_CONTROL`     | `0x02` | `0x47800008`  | Soft reset + clk gate.                     |
| `AUD_DAC_CONFIG0`     | `0x07` | `0x4780001C`  | HPF bypass, gain (0..0x3F), clk_invert.    |
| `AUD_FIFO_CONFIG`     | `0x0A` | `0x47800028`  | DAC L/R read threshold + int enable.       |
| `AUD_FIFO_STATUS`     | `0x0E` | `0x47800038`  | DAC/ADC/DTMF full/empty flags.             |
| `AUD_DAC_FPORT`       | `0x12` | `0x47800048`  | 32-bit FIFO data port (GDMA destination).  |
| `AUD_EXTEND_CFG`      | `0x18` | `0x47800060`  | DAC frac-div manual, EQ filter enable.     |
| `AUD_DAC_FRACMOD`     | `0x19` | `0x47800064`  | 32-bit DAC frac-div coefficient.           |
| `AUD_AUDIO_CONFIG`    | `0x30` | `0x478000C0`  | Global enables + `samp_rate_DAC` + APLL.   |

### 1.2 Bit fields we actually touch

`AUD_AUDIO_CONFIG` (0x30):

| Bit  | Name           | Meaning                                      |
|------|----------------|----------------------------------------------|
| 2    | `DAC_ENABLE`   | 1 = DAC clock/data flowing.                  |
| 7:6  | `SAMP_RATE_DAC`| 0=8k, 1=16k, 2=44.1k, 3=48k.                 |
| 8    | `APLL_SEL`     | 1 = 24.576 MHz APLL, 0 = 26 MHz XTAL.        |

`AUD_DAC_CONFIG0` (0x07):

| Bits  | Name            | Meaning                                       |
|-------|-----------------|-----------------------------------------------|
| 16    | `DAC_HPF2_BYPASS` | 1 = bypass DAC HPF stage 2.                 |
| 17    | `DAC_HPF1_BYPASS` | 1 = bypass DAC HPF stage 1.                 |
| 23:18 | `DAC_SET_GAIN`  | Digital gain 0..0x3F, default `0x2D` (=45).   |
| 24    | `DAC_CLK_INVERT`| Optional clock invert.                        |

`AUD_FIFO_CONFIG` (0x0A):

| Bits | Name                    | Meaning                                     |
|------|-------------------------|---------------------------------------------|
| 4:0  | `DACR_RD_THRESHOLD`     | DAC right FIFO read-below-threshold IRQ.    |
| 9:5  | `DACL_RD_THRESHOLD`     | DAC left FIFO read-below-threshold IRQ.     |
| 20   | `DACR_INT_EN`           | 1 = enable DAC-R IRQ.                       |
| 21   | `DACL_INT_EN`           | 1 = enable DAC-L IRQ.                       |

`AUD_EXTEND_CFG` (0x18):

| Bit | Name                  | Meaning                                       |
|-----|-----------------------|-----------------------------------------------|
| 0   | `DAC_FRACMOD_MANUAL`  | 1 = use `AUD_DAC_FRACMOD` as divider.         |
| 2   | `FILT_ENABLE`         | Optional per-DAC EQ filter (kept off).        |

### 1.3 Sys-ctrl bits used by DAC bring-up

All sits behind analog registers (ana_regN) written via the SPI window.  On
BK7258 the "hi-level" ops map as:

| Op                              | Analog reg / field                                 |
|---------------------------------|----------------------------------------------------|
| `sys_drv_aud_int_en(1)`         | Audio-block interrupt enable in sys ctrl core.     |
| `sys_drv_aud_audbias_en(1)`     | `ana_reg18[ENAUDBIAS]` → audio bias.               |
| `sys_drv_aud_dac_bias_en(1)`    | `ana_reg18[DACFB2ST0V9]`-style bias fields.        |
| `sys_drv_aud_dac_drv_en(1)`     | `ana_reg20[DACDRVEN]`.                             |
| `sys_drv_aud_dac_dcoc_en(1)`    | `ana_reg20[DCOC*]`.                                |
| `sys_drv_aud_dac_idac_en(1)`    | `ana_reg21[ENIDACL]`.                              |
| `sys_drv_aud_dacl_en(1)`        | `ana_reg20[DACLEN]`.                               |
| `sys_drv_aud_dacr_en(1)`        | (right-channel DAC en; same reg family).           |
| `sys_drv_aud_diffen_en(1)`      | Differential vs single-ended output selector.      |
| `sys_drv_aud_dac_dacmute_en(x)` | Master mute in DAC block.                          |
| `sys_drv_apll_en(1)`            | `ana_reg5[PWDAUDPLL]` (write inverted).            |
| `sys_drv_apll_cal_val_set(v)`   | `ana_reg26 = v`.                                   |
| `sys_drv_apll_config_set(v)`    | `ana_reg25 = v`.                                   |
| `sys_drv_apll_spi_trigger_set(v)` | `ana_reg25[SPI_TRIGGER]`.                        |
| `sys_drv_aud_select_clock(v)`   | `cpu_clk_div_mode1.cksel_aud`, sys ctrl.           |

> **⚠ AVDK precise mapping:** `bk_avdk_smp/ap/middleware/soc/bk7258_ap/hal/sys_hal.c`
> lines 1809–2087 give the exact `sys_ll_set_*` wired to each `sys_hal_aud_*`
> op.  Implementation MUST call the equivalent `putreg32/getreg32` sequence,
> not re-derive bits from field names.

## 2. Bring-up sequence (48 kHz S16_LE stereo, APLL clock)

### 2.1 One-time module init  (`bk_aud_driver_init`)

1. Power-on the AUDP subsystem (delegated to the SoC power domain; on the
   locked bring-up we assume it is already powered).  ⚠ AVDK calls
   `bk_pm_module_vote_power_ctrl(AUDP_AUDIO, ON)` and
   `bk_pm_clock_ctrl(PM_CLK_ID_AUDIO, PWR_UP)`.
2. `sys_drv_aud_select_clock(0)` — start from XTAL, APLL is muxed in later
   by `bk_aud_clk_config`.
3. Register the audio ISR at `INT_SRC_AUDIO` (NuttX side: our own IRQ handler,
   `up_enable_irq()` after `irq_attach()`).
4. `sys_drv_aud_int_en(1)` — gate audio interrupts on in sys ctrl.
5. Soft-reset the AUD clock control: write `AUD_CLK_CONTROL |= 1`, then
   clear.  This mirrors `aud_hal_set_clk_control_soft_reset(1)`.
6. Program `ana_reg18/19/20/21` and `ana_reg27` to the per-field default
   composition produced by `ana_reg18_value_cal()` .. `ana_reg27_value_cal()`
   in `aud_common_driver.c:153..291`.

   ⚠ **Do not re-invent this.** Copy the value builders verbatim (with each
   `SYS_ANA_REGxx_<field>_DEFAULT_VAL` constant taken from
   `bk_avdk_smp/ap/middleware/soc/bk7258_ap/soc/sys_reg.h`, 189 field
   constants total).  These composite values are the analog block reset
   defaults that BK7258 silicon expects; deviating from them will change bias
   current, VCM and DAC common-mode.

7. `sys_drv_aud_audbias_en(1)` — turn on the audio bias network.  This must
   happen after step 6 (ana_reg18 has been written first).

### 2.2 Clock select (`bk_aud_clk_config(AUD_CLK_APLL)`)

1. `sys_drv_aud_select_clock(1)` — route APLL to the audio clock mux.
2. `sys_drv_apll_en(1)` — power up APLL (clear `ana_reg5[PWDAUDPLL]`).
3. `sys_drv_apll_cal_val_set(0x8973CA6F)` — APLL cal for 8/16/32/48 kHz
   families (`0x88AF2EC9` if the target rate is 11.025/22.05/44.1 kHz).  ⚠
4. `sys_drv_apll_config_set(0xC2A0AE86)` — APLL config word.  ⚠
5. `sys_drv_apll_spi_trigger_set(1)`.
6. `delay(10)` — busy-loop ~10 ticks for APLL to lock.  ⚠ Preserve as
   `up_udelay(200)` at the least; refine on scope.
7. `sys_drv_apll_spi_trigger_set(0)`.
8. `AUD_AUDIO_CONFIG[8] = 1` (APLL_SEL).

### 2.3 DAC block init (`bk_aud_dac_init(48000/16bit/stereo/diff)`)

1. `bk_aud_driver_init()` (2.1) must have completed OK.
2. `bk_aud_clk_config(AUD_CLK_APLL)` (2.2).
3. `sys_drv_dmic_clk_div_set(0x1)` — required only when clock source is
   APLL; sets DMIC divider so it does not steal APLL bandwidth.
4. Power up DAC analog path (order matters, all sys_drv_* below):
   - `aud_dac_bias_en(1)`
   - `aud_dac_drv_en(1)`
   - `aud_dac_dcoc_en(1)`
   - `aud_dac_idac_en(1)`
5. Channel select — for stereo `AUD_DAC_CHL_LR`:
   - `aud_dacl_en(1)`
   - `aud_dacr_en(1)`
6. Differential vs single-ended: `aud_diffen_en(1)` for HT6873 (BK7258
   Datasheet §Audio recommends differential to 600 Ω load).  Board layer
   ultimately owns this; playback default is differential.
7. `AUD_DAC_CONFIG0`:
   - `DAC_SET_GAIN = 0x2D`  (≈ −6 dB relative to full scale, AVDK default).
   - `DAC_CLK_INVERT = 0`.
   - `DAC_HPF1_BYPASS = 1`, `DAC_HPF2_BYPASS = 1`.
8. Sample rate — 48 kHz path:
   - `AUD_EXTEND_CFG[0] = 0` (`DAC_FRACMOD_MANUAL` off — 48k uses the
     integer divider).
   - `AUD_AUDIO_CONFIG[7:6] = 0b11`  (samp_rate_DAC = 3).
9. Do **not** set `DAC_ENABLE` yet — leave DAC muted with
   `sys_drv_aud_dac_dacmute_en(1)` until FIFO has been primed by GDMA.

#### Sample-rate table

| Fs (Hz) | `AUD_AUDIO_CONFIG[7:6]` | `EXTEND_CFG[0]` | `AUD_DAC_FRACMOD`      |
|---------|-------------------------|-----------------|------------------------|
| 8000    | `0`                     | 0               | —                      |
| 11025   | `2`                     | 1               | `CONST_DIV_44_1K << 2` |
| 12000   | `3`                     | 1               | `CONST_DIV_48K   << 2` |
| 16000   | `1`                     | 0               | —                      |
| 22050   | `2`                     | 1               | `CONST_DIV_44_1K << 1` |
| 24000   | `3`                     | 1               | `CONST_DIV_48K   << 1` |
| 32000   | `1`                     | 1               | `CONST_DIV_16K   >> 1` |
| 44100   | `2`                     | 0               | —                      |
| 48000   | `3`                     | 0               | —                      |

With:

```
CONST_DIV_16K   = 0x06590000
CONST_DIV_32K   = 0x01964000
CONST_DIV_44_1K = 0x049B2368
CONST_DIV_48K   = 0x043B5554
```

⚠ For any 11/22/44.1 kHz rate, `sys_drv_apll_cal_val_set(0x88AF2EC9)` must be
re-applied and the SPI trigger cycled (see 2.2).

### 2.4 FIFO + GDMA plumbing

1. `AUD_FIFO_CONFIG[9:5]  = DACL_RD_THRESHOLD` — 4 for BK7256, unspecified
   for BK7258.  Recommendation: start at `8` (FIFO half-empty) and tune on
   scope.
2. `AUD_FIFO_CONFIG[4:0]  = DACR_RD_THRESHOLD` — same value.
3. `AUD_FIFO_CONFIG[21]   = DACL_INT_EN = 1` **only if** we intend to use
   CPU-fed writes via `AUD_DAC_FPORT`.  For GDMA-fed playback, keep both
   `DAC?_INT_EN = 0` and drive the FIFO from a GDMA channel targeting
   `AUD_DAC_FPORT_ADDR (0x47800048)` with a request line wired to DAC-L.
4. GDMA:
   - Channel: use the BK7258 general_dma block (`SOC_GENERAL_DMA_REG_BASE`,
     to be resolved in the GDMA chip driver — separate spec).
   - Source: DMA-safe cache-line-aligned buffer in AP RAM.
   - Dest: `AUD_DAC_FPORT_ADDR`.
   - Width: 32-bit; for S16_LE stereo, each 32-bit word contains
     `{right[15:0], left[15:0]}` in the low half-words — verify layout on
     scope with a known asymmetric test tone.
   - Mode: double buffer, circular.  Half-transfer + full-transfer IRQs
     drive `AUDIO_CALLBACK_DEQUEUE` on the corresponding `ap_buffer_s`.
5. Cache: after `AUDIOIOC_ALLOCBUFFER`, buffers live in cached AP RAM.
   Playback (write direction) needs `up_clean_dcache(buf, buf+n)` before
   handing the buffer to the GDMA channel.

### 2.5 Start

1. Ensure at least two buffers are enqueued to GDMA (avoids underrun on the
   very first period).
2. Board layer: `bk7258_pa_disable()`.  Guarantees the PA is off during
   glitch-prone start-up.
3. `AUD_AUDIO_CONFIG[2] = DAC_ENABLE = 1`.
4. Wait until FIFO status shows both DAC L/R non-empty
   (`AUD_FIFO_STATUS_ADDR`).  ⚠ AVDK does not busy-poll here; instead it
   relies on the pre-loaded FIFO.  Recommendation: poll for up to 2 ms
   before proceeding.
5. Board layer: `bk7258_pa_enable()`.  The board callback owns the
   PA-on settling delay (typically 20–50 ms) before returning.
6. Volume ramp: raise `DAC_SET_GAIN` from `0x00` to the target (`0x2D` for
   0 dB) in ~10 ms.  This avoids the click that comes from turning on a
   full-scale DC-offset DAC into an already-enabled PA.
7. `sys_drv_aud_dac_dacmute_en(0)` — final unmute.

### 2.6 Stop

Reverse order, symmetric:

1. Volume ramp `DAC_SET_GAIN` → 0 over ~5 ms.
2. `sys_drv_aud_dac_dacmute_en(1)`.
3. Board: `bk7258_pa_disable()` and wait its off-settling delay.
4. `AUD_AUDIO_CONFIG[2] = 0`.
5. Stop GDMA channel, invalidate/complete any in-flight `ap_buffer_s`.
6. If shutting down the whole device (not just a single stream):
   - `aud_dacr_en(0)`, `aud_dacl_en(0)`,
   - `aud_dac_bias_en(0)`, `aud_dac_drv_en(0)`, `aud_dac_dcoc_en(0)`,
     `aud_dac_idac_en(0)`,
   - `sys_drv_aud_int_en(0)`, unregister ISR,
   - Optionally power down the AUDP domain.

## 3. Volume / mute mapping (NuttX AUDIO_FU_VOLUME / AUDIO_FU_MUTE)

| NuttX volume | Register action                                                            |
|--------------|----------------------------------------------------------------------------|
| 0            | `DAC_SET_GAIN = 0` **and** `sys_drv_aud_dac_dacmute_en(1)`.                |
| 1..100       | `DAC_SET_GAIN = table_lookup(v)` with a dB curve; unmute if muted.         |

Suggested curve endpoints: `v=1 → 0x08 (≈ −54 dB)`, `v=100 → 0x2D (≈ 0 dB)`.
Do not exceed `0x3F`.  Analog gain (`sys_drv_aud_dacg_set`) is set once at
init and not touched on volume changes to avoid zipper noise.

`AUDIO_FU_MUTE = 1` maps directly to `dacmute_en(1)` without touching gain.

## 4. Error and abort paths

All of the following MUST result in PA-off + DAC muted before returning:

- `configure` failure at any step of 2.3.
- GDMA channel allocation failure.
- GDMA transfer error / underrun IRQ.
- `stop()` from upper half.
- `shutdown()` from upper half.
- File-descriptor close on abnormal termination.

Underrun handling: on GDMA underrun IRQ, emit
`AUDIO_CALLBACK_UNDERRUN`, keep the DAC enabled but mute, wait for the next
enqueue to re-prime the FIFO, then unmute.

## 5. Deviations we accept from AVDK

- AVDK feeds the FIFO via `bk_aud_dac_write()` (CPU polling of
  `AUD_FIFO_STATUS`).  We only use that path for early bring-up smoke
  tests; production is GDMA-only.
- AVDK sets DAC gain from `aud_dac_config_t.dac_gain` at init and never
  changes it.  We drive it from `AUDIOIOC_CONFIGURE`/`AUDIO_FU_VOLUME`.
- AVDK owns the AUDP power/clock domain via `bk_pm_*`.  The initial NuttX
  port keeps the AUDP domain permanently powered while the driver is
  loaded; a later PM revision can revisit this.
- AVDK combines DAC + ADC + DTMF + DMIC in one common driver init.  We
  only compile the DAC path; ADC/DMIC/DTMF ana_reg fields are still written
  because they share `ana_reg18/19/27`, but their sys_drv_aud_micX_en et al.
  stay at reset (mic off).

## 6. Traceability

Every rule above traces back to a specific AVDK file:

| Section | Source                                                                 |
|---------|------------------------------------------------------------------------|
| 2.1     | `middleware/driver/audio/aud/aud_common_driver.c:354..405`              |
| 2.2     | `middleware/driver/audio/aud/aud_common_driver.c:316..343`              |
| 2.3     | `middleware/driver/audio/aud/aud_dac_driver.c:54..135`                  |
| 2.4     | `middleware/soc/bk7258_ap/hal/aud_ll_macro_def.h` (register bits)       |
| 2.5/2.6 | `middleware/driver/audio/aud/aud_dac_driver.c:372..390` + our PA ops   |
| 3       | `middleware/driver/audio/aud/aud_dac_driver.c:242..268`                 |
| 4       | Our derivation from `AUDIO_CALLBACK_UNDERRUN` semantics.                |

Anything not covered here (I2S, ADC, DTMF, EQ filter loading) is out of
scope for the initial `/dev/audio/pcm0p` bring-up.
