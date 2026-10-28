Narration transcripts and word timestamps are local, ignored inputs and are not
included in the source repository. Native in-game dialogue subtitles still use
text from the owner's ISO. For optional local retiming, put proofread text in
cue_text.json with {"intro": ["cue text"], "outro": ["cue text"]} beside your
locally generated word timestamps. Keep all resulting SRT/JSON/audio private.

Historical local asset provenance:
intro_en.srt / outro_en.srt: offline Whisper (faster-whisper base.en, int8, CPU) transcripts of the movie soundtracks "D2 intro audio01" / "D2 Outro Audio01"
(LEVEL0RO.SDB 0x304 id 99/100 sub 14). Names proofread by hand; "[NAME CHECK: x]" = name not verifiable from the disc text.
Game text derived from the user's disc: personal use, do not redistribute.
Re-create: python -I analysis/scripts/sub_adpcm_wav.py <LEVEL0RO.SDB> 99 14 intro.wav ; then (venv with faster-whisper):
  w=wave.open(path); a=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(np.float32)/32768; resample to 16000 with np.interp;
  segs,_=WhisperModel("base.en",compute_type="int8").transcribe(a,language="en",beam_size=5)   # pass a numpy array: faster_whisper.decode_audio crashes with the installed av (metadata_errors)

2026-10-17 (branch drakan-snd): intro_en.srt / outro_en.srt were re-timed from word-level Whisper timestamps (intro_words.json / outro_words.json, small.en) with
analysis/scripts/sub_retime.py (python -I sub_retime.py patches/proposed/subtitles); the shipped copies are dist/DrakanPC/subtitles/intro_english.srt / outro_english.srt.
Cue time 0 = first sample of the soundtrack = the SPU2 key-on of the stream voice. See docs/reference/subtitles.md section 9.

2026-10-17 subtitle-gap follow-up (PS2Recomp-sub): the source SRTs now use touching cues, delayed starts (100 ms after estimated speech onset), and verified
onset floors at intro cues 1/17 (6.330 / 80.380 s, from rendered-audio recognition). Known proper-name spellings are matched rather than interpolated.
Regenerate with tools/PS2Recomp-sub/scripts/narration_timing.py, not the historical sub_retime.py (which would restore the old early starts and 40 ms gaps).
The narration adapter in that worktree also suppresses fades at touching cue boundaries; integrate the adapter and these assets together.
See tools/PS2Recomp-sub/docs/NARRATION_GAPS.md for measurements, raw-log locations and limitations. Shared source asset edits are left for the integration
owner to review/stage; dist has NOT been updated by this follow-up.
