# ComfyUI texture workflows

Drag a JSON file onto the ComfyUI canvas to load it.

## `texture-upscale-realesrgan-4x.json`

Faithful 4x upscale with the installed `RealESRGAN_x4plus.safetensors`. It has no text prompt. The original PNG's alpha mask is reapplied to the output.

## `texture-remaster-flux2-klein-4b.json`

Prompt-guided image editing based on [ComfyUI's official FLUX.2 Klein 4B distilled template](https://github.com/Comfy-Org/workflow_templates/blob/main/templates/image_flux2_klein_image_edit_4b_distilled.json). It first upscales the input with RealESRGAN, then gives that image and an editable description to FLUX. The workflow is preloaded with the biohazard sign example. Replace both the Load Image selection and the prompt when trying another texture.

Required model files in ComfyUI:

| Folder | File | Source |
| --- | --- | --- |
| `models/diffusion_models` | `flux-2-klein-4b-fp8.safetensors` | [Black Forest Labs](https://huggingface.co/black-forest-labs/FLUX.2-klein-4b-fp8) |
| `models/text_encoders` | `qwen_3_4b_fp4_flux2.safetensors` | [Comfy-Org](https://huggingface.co/Comfy-Org/vae-text-encorder-for-flux-klein-4b/tree/main/split_files/text_encoders) |
| `models/vae` | `flux2-vae.safetensors` | [Comfy-Org](https://huggingface.co/Comfy-Org/vae-text-encorder-for-flux-klein-4b/tree/main/split_files/vae) |
| `models/upscale_models` | `RealESRGAN_x4plus.safetensors` | Existing texture workflow |

The prompt-guided workflow targets about 0.25 megapixels so it can run on a 12 GB RTX 3080 Ti. A local test on the 38x38 biohazard input completed at 512x512 in about 15 seconds and used about 10.3 GB VRAM. It can redraw shapes; review the output against the original before including it in a pack. The final alpha comes from the original input.

The `GOLDENEYE#...` filename is a checksum and rendering key, not a description. `texture_names.py` builds a keyed map from exact ROM texture IDs, existing pilot descriptions, human review notes, and `texture_name_overrides.json`. Each description records its source and whether it needs review. Numeric ROM labels are left unnamed. Descriptions are hints: the image remains the authority for shape and layout.

From the repository root, with the ROM and headset dumps under `build/`:

```powershell
python tools/texai/texture_names.py build
python tools/texai/texture_names.py lookup 'GOLDENEYE#3B30F88F#2#1#00253906_ciByRGBA.png'
python tools/texai/contextual.py list --limit 20
python tools/texai/contextual.py run --name 'GOLDENEYE#3B30F88F#2#1#00253906_ciByRGBA.png'
```

`contextual.py` sends the mapped description with the source image to the local FLUX workflow. It writes review candidates to `build/texai-contextual/candidates/`, and never edits the shipped pack. To process all eligible named textures, use `python tools/texai/contextual.py run` while ComfyUI is running. Curated descriptions run first; existing candidates are skipped unless `--force` is given. Previous rejected textures, effects, lettering, and image fragments are skipped by default; `--include-review-notes` opts into them. Every generated candidate still needs visual review, especially signs, faces, numbers, and text.

Most dumped textures lack descriptive ROM names. To curate them, generate a numbered contact sheet and its matching JSON index:

```powershell
python tools/texai/texture_names.py sheet --page 0
```

Add the inspected filename as a key in `tools/texai/texture_name_overrides.json`, with a `description` of the visible subject and a `category` such as `material`, `object`, `screen`, `symbol`, or `face`. Set `review_required: true` when the identification is uncertain. Rebuild the map before running more jobs. Do not fill unknown entries from an unreviewed image captioner: a local test misidentified both the biohazard icon and several known game textures.

For exact symbols, numbers, and lettering, manual or vector redraw is usually more reliable than generative editing.
