# Experimental RADEV1/RADEV2 integration

This source tree retains `FreeDV 3.0.0-dev-W2MWS-radev2.1`. The implementation
has not been compiled or tested as part of this change.

## References and dependency boundary

- W2MWS Phase 1 baseline: `832af6e7`.
- The common backend remains pinned at `9922bba3aed07c4b3d13ef7a4b003f917f253ded`.
- RADEV2 GUI reference: `drowe67/freedv-gui`, `ms-rade-v2`, inspected at
  `4a3d37ba95ebf3f18c375b4966ff78e269e3f0fb`.
- RADEV2 pipeline/text reference: `freedv/freedv-backend`, `radev2-dev`,
  `a1f2b4331c375cd73575616e78fa6993ffc16115`.
- The backend's previously floating `rade_c` dependency is frozen at
  `c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9`, the inspected dual-mode revision
  already present in the local Phase 1 build tree. `cmake/PinRADE.cmake`
  changes only the fetched backend's RADE dependency declaration. It does not
  replace backend pipeline, Reporter, audio, or utility sources.

The new files in `src/pipeline/radev2` are adapted from the pipeline/text
reference above, retaining its license. Class, text API, and internal text
type names are distinct from the backend's RADEV1 names. `ldpc_code.h` is the
reference's per-instance LDPC implementation; it uses the unchanged parity
matrix exposed by the pinned backend. No RADEV2 backend-wide merge is needed.

## Selection and active session

`/Modem/SelectedRadeMode` remains 1 (RADEV1) or 2 (RADEV2). Missing/invalid
values still default to RADEV1. The Control panel remains shared between
Notebook and Independent workspaces. Changes save without starting the modem,
and remain locked while starting, running, and stopping.

`FreeDVInterface::start()` accepts a typed mode and reports success. It opens
one RADE instance, using `RADE_MODE_V2` only for RADEV2. The pinned RADE API
uses built-in V1/V2 weights and ignores the model filename. It initializes
only the chosen mode's encoder, decoder, and (for V2) synchronization model;
the common LPCNet feature extractor and FARGAN speech synthesizer are also
initialized for that session. Both modes' read-only weight tables are present
in the library; there are no external RADEV2 model files to install.

The active mode is published only after successful initialization. UI status
and Reporter obtain the protocol name from that session, not the persisted
selection. Initialization failure stops startup with a message and releases
partially allocated resources; there is no fallback to another codec. Normal
shutdown retains the existing ordering: stop/join processing and destroy the
pipelines before closing their borrowed codec/text resources. Cleanup is
safe to repeat after an unsuccessful start.

## Transmit, receive, and text

RADEV1 continues to use the pinned backend's `RADETransmitStep`,
`RADEReceiveStep`, and `rade_text` implementation. Its scaling, frame queries,
eight-character callsign handling, EOO data buffer, and EOO QPSK/LDPC text
framing are preserved.

RADEV2 uses the separately named `RADEV2TransmitStep` and `RADEV2ReceiveStep`.
They size feature/modem/EOO buffers from the opened instance's API queries.
The receive output bound reserves space for every speech frame a single
decode can emit. TX/RX sample conversion follows the RADEV2 reference's
`RADE_INT16_SCALE` convention. The real-time steps retain preallocated buffers.

RADEV2 text repeats LDPC(112,56) BPSK codewords at one data symbol per modem
frame (about 25 bit/s). TX calls `rade_tx_set_data_symbol()` before modulation;
RX feeds `rade_rx_get_data_symbol()` into its independent decoder. It supports
up to four eight-character blocks, with CRC and block reassembly, and resets
RX combination/reassembly state on loss of sync or pipeline reset. A short
over may finish before a callsign codeword can be decoded.

RADEV2 still emits EOO for modem shutdown, but carries no text in that frame.
Only the V2 receive step passes a null EOO data buffer to `rade_rx()`. RADEV1
always keeps the backend's allocated EOO buffer, including when reporting is
disabled. The existing W2MWS short-PTT/VOX drain logic, resampling, audio
routing, Reporter endpoints, and decoded-speech hooks are unchanged.

## Local validation to perform

Reconfigure CMake before compiling: new pipeline sources and a changed
dependency patch step must be generated. Prefer a fresh experimental build
directory. Existing fetched-source overrides or disconnected dependency
builds must also use the pinned codec revision; do not reuse an older
`librade.dll` beside the new executable. The LLVM-MinGW toolchain and Opus pin
are unchanged, and the added sources use the existing pipeline library.

No build, tests, or CMake reconfiguration were run for this implementation.
After compiling locally:

1. Run `ConfigMigrationTest`, `FreeDVInterfaceTest`, `RadeV2TextTest`, and the
   existing `TxRxThreadTest` plus backend RADEV1 loopback/text regression tests.
   The new interface cases check V2 frame sizes, V2-to-V1 restart, V2 voice
   with reporting disabled, and streamed callsign reception before EOO.
2. Run GUI scenarios `rade_mode_selection`, `rade_mode_startup`, and
   `rade_mode_while_running` in `test/gui/test_main_window.py`. The latter
   scenarios require the existing virtual-audio test setup. They cover both
   workspaces, manual/automatic startup, session switching, labels, and locking.
3. Against an independent RADEV1 peer, compare speech, synchronization, SNR,
   EOO callsign reports, Reporter/watchlist updates, and WSJT-X logging with
   W2MWS.14. Verify callsigns still arrive at end of over.
4. Against the pinned RADEV2 reference peer, test voice in both directions,
   short and compound callsigns during a sufficiently long over (20-40 s),
   reporting disabled, EOO, loss/reacquisition of sync, and repeated PTT.
   Verify Reporter and status identify the actual codec as RADEV2.
5. In both modes check very short PTT, normal PTT, VOX drain, Tune, voice
   keyer, analog passthrough, 44.1/48 kHz routing, full duplex, and stop/close
   during TX. Confirm PTT releases and no EOO samples or audio are stranded.
6. Switch V1/V2 between sessions repeatedly in Notebook and Independent
   workspaces. Check snapping, saved geometry/appearance, auxiliary windows,
   Decoded Speech/Whisper, and unchanged experimental title/About identity.

Compatibility and performance remain subject to these local checks. There
is no throwaway inference warm-up instance; check cold-start TX/RX latency
and audio underruns under load as well as steady-state operation. There
is no automatic on-air mode detection: both stations must choose the same
mode. The experimental text format tracks the pinned reference, not an
assumption that future `radev2-dev` revisions will remain compatible.
