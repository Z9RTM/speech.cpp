# FASTDIAR clustering trial

This is the first native stage of a FASTDIAR port: CPU streaming online clustering of normalized frame embeddings.
It is not a FASTDIAR model engine. Neither `speech diarize` nor `speech asr --diarize` can load FASTDIAR yet.
The encoder, frontend, VAD integration, GGUF reader/converter and dynamic speaker API remain unimplemented.

The reference is [microsoft/fastdiar](https://github.com/microsoft/fastdiar) at
`f48bff53b9a6a0cce4539e94b6def95f609eaea4`. Its Microsoft MIT license is retained beside this reference.
The native stage follows `OnlineClustering` with `post_process=False`. It keeps the full history and exact
coordinate medians; it does not impose an eight-speaker limit. Output labels are internal cluster IDs,
not the arrival-ordered public speaker labels assigned by `StreamingDiarizer`.

```sh
git clone https://github.com/microsoft/fastdiar /tmp/fastdiar
git -C /tmp/fastdiar checkout f48bff53b9a6a0cce4539e94b6def95f609eaea4
uv run --project reference/fastdiar reference/fastdiar/dump.py \
  --source /tmp/fastdiar --out reference/fastdiar/out/clustering
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target fastdiar-clustering-check -j4
build/fastdiar-clustering-check reference/fastdiar/out/clustering/*
```

`dump.py --embeddings result.npz` additionally checks embeddings and VAD decisions recorded by an official
`StreamingDiarizer` run (`embeddings` float32, `vad` uint8). The check compares every internal cluster ID, its frame,
and the input frame that makes it final, including flush, exactly. It also covers twelve returning voices,
no merging, forced decisions, online merges, silence, an empty input, a short tail and a zero-delay window.
The Python reference intentionally retains the official short-tail and silence-finalization behavior.

For a direct build of this CPU-only check without ggml:

```sh
c++ -std=c++17 -O2 -Ichecks -Isrc/families \
  checks/fastdiar/fastdiar-clustering-check.cpp src/families/fastdiar/clustering.cpp \
  -o /tmp/fastdiar-clustering-check
```

Whole-model performance and DER trials belong in speech-bench. They are not measurements of this native stage.
