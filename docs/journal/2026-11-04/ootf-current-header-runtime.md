# OotF current-header runtime acceptance

The fresh normal-NewGame run uses the audited header-corrected pack `CD09A5D52AFA45C6FBCBE148B0F9AE2B1162E9F513C4E2CFD51D18FC87C0825C` and staged executable `6D747DB43DFD7C50C39781F5DDD973C6834654C7BF409D8C87CA0A1E0C116D46`. Evidence is under ignored `build/scratch/ootf-header-fixed/normal-header-fixed`. This uses D3D11, ray effects off, explicit world rate 60, an empty private card, and the normal title game selector. There is no forced level exit, position edit, cinematic skip, or health override. Harness audio is disabled, so this run does not verify speech playback.

All original opening scenes finished naturally. Control returned and native F5 completed successfully at timestamp 304.695. The private Slot33 RSG is 165,822 bytes. Normal D/W/A movement moved Rynn from approximately `(219033,19231,170358)` to `(209775,19499,173425)`; mouse input changed body yaw from 1.4051 to 1.2050. The HUD is visible and the native input profile reports play with current player `0x975180`. Earlier cached Intro-player telemetry is not used as a player-corruption test.

The heap census walks actual guest chunks to the allocator top and reads live actor registry resource/class records. All samples reach the top. Every sample contains zero native Dust Devil generators (resource low id 0x1890, runtime class 0x1003f) and zero Dust Devil particle actors (0x187a/0x18ec, class 0x1007c). The preceding failed candidate had approximately 61 generators throughout the opening and increasing dust pairs.

| Sample | In-use guest heap bytes | 5,648-byte particle pages |
| --- | ---: | ---: |
| RV15 label (timestamp 107.346) | 11,622,944 | 17 |
| RV75 | 14,464,176 | 30 |
| RV135 | 15,214,048 | 37 |
| Quicksave | 15,716,352 | 25 |
| After movement/mouse | 16,254,304 | 26 |
| Soak 60 | 16,304,704 | 26 |
| Soak 120 | 16,325,824 | 26 |
| Soak 180 | 16,325,760 | 26 |

At timestamp 520 the game remains in play with advancing native look callbacks, zero active opening sequences, and no watchdog fault. The old indexed/class-reference run stalled around timestamp 331 after reaching the approximately 26 MB guest heap ceiling. This establishes removal of the specific unintended-generator accumulation in this run; it is not a full campaign or visual-fidelity claim. The original native sound/animation omissions, unsupported class diagnostics, and later campaign progression remain separate work.

The planned script quit completed with actual process exit 0 after 541 wall seconds. Final RAM has 16,325,760 in-use bytes, 26 particle pages, no unwanted actors, and the same allocator top as soak120/180. No watchdog dump was emitted. Native save/load roundtrip remains the next independent check. The fresh checkpoint has been handed to the progression agent for legitimate traversal; old checkpoints made with faulty current headers are excluded.

## Native copied-card save/load roundtrip

The follow-up `build/scratch/ootf-header-fixed/native-roundtrip-final` uses the same executable and pack with a private copy of the fresh card. It selects OotF at the normal title, presses F8 to load Slot33, moves with D, presses F5, moves away with A, presses F8 again, then moves with W. It waits for the native load/save screen followed by 60 consecutive play samples before recording each completed operation. Both loads and the save report native return to gameplay. Actual process exit is 0 after 59 wall seconds.

Current context actor handles, rather than the potentially stale cached input-controller pointer, establish the roundtrip:

| Stage | Player position (world units) |
| --- | --- |
| Initial native load | 219033.109375, 19231.029297, 170358.343750 |
| D movement and new native save | 219739.140625, 19248.496094, 166114.390625 |
| A displacement after save | 219049.234375, 19248.486328, 170251.687500 |
| Second native load | 219739.140625, 19248.496094, 166114.390625 |
| W movement after reload | 217346.359375, 19228.597656, 165714.093750 |

The saved and reloaded positions match exactly, including float representation. Reload allocates a new current player handle (`0xb64540` versus `0x9ee4d0`) and subsequent real movement works. All six completed-stage RAM samples have zero unintended generators/dust actors and valid allocator walks; final in-use heap is 14,337,024 bytes. The original normal-opening card remains unchanged for the progression agent.

An initial roundtrip also succeeded but captured its second RAM image during GUI14 loading. A follow-up recipe then mistakenly required 120 consecutive native listing-screen samples and waited after a successful load; that private run was stopped explicitly and is not an acceptance result. The final recipe uses one listing-screen sample before its stable gameplay wait. These are harness timing corrections, not demonstrated game failures. Audio remains disabled and later campaign progression is still unverified.
