#!/usr/bin/env python3
"""
Run texai's textures through local upscalers in a running ComfyUI (its HTTP
API, default http://127.0.0.1:8188), and save each answer in texai's framing
so `texai.py post/sheet` treat it like a ChatGPT or Gemini answer.

    python tools/texai/comfy.py <orig_dir> <work_dir> seedvr2|esrgan|esrgan-seedvr2 [tex ...]

Needs <work_dir>/in/manifest.json from `texai.py prep`. Models (ComfyUI
models/ folder, from the Comfy-Org Hugging Face repos):
    seedvr2: diffusion_models/seedvr2_7b_int8_convrot.safetensors, vae/seedvr2_ema_vae_fp16.safetensors
    esrgan:  upscale_models/RealESRGAN_x4plus.safetensors

Unlike the chat models, these get the texture at its own size (upright, the
transparent texels on the key colour), padded by PAD texels: wrapped around
on axes that repeat, edge-repeated on clamped ones, so the upscale sees the
neighbours it will have in the game. The padding is cropped off afterwards.
"""

import io
import json
import os
import sys
import time
import uuid
import urllib.parse
import urllib.request

import numpy as np
from PIL import Image

HOST = os.environ.get('COMFY_HOST', 'http://127.0.0.1:8188')
PAD = 8
KEY = (255, 0, 255)
REGION = 1024      # SeedVR2 output size for the texture's longer side (like the chat models' answers)


def call(path, data=None, headers=None):
    req = urllib.request.Request(HOST + path, data=data, headers=headers or {})
    with urllib.request.urlopen(req) as r:
        return r.read()


def upload(img, name):
    buf = io.BytesIO()
    img.save(buf, 'PNG')
    b = uuid.uuid4().hex
    body = (('--%s\r\nContent-Disposition: form-data; name="image"; filename="%s"\r\n'
             'Content-Type: image/png\r\n\r\n' % (b, name)).encode() + buf.getvalue() +
            ('\r\n--%s\r\nContent-Disposition: form-data; name="overwrite"\r\n\r\ntrue\r\n--%s--\r\n' % (b, b)).encode())
    return json.loads(call('/upload/image', body, {'Content-Type': 'multipart/form-data; boundary=' + b}))['name']


JOB_LIMIT = 240   # seconds; a job past this has spilled out of VRAM (minutes a texture) or hung


def free():
    """Unload ComfyUI's models and cached memory. Its VRAM use crept up over a
    batch until a 33x33 texture that takes 11 s ran for minutes, spilling."""
    call('/free', json.dumps({'unload_models': True, 'free_memory': True}).encode(),
         {'Content-Type': 'application/json'})


def run(graph, save_node):
    pid = json.loads(call('/prompt', json.dumps({'prompt': graph, 'client_id': uuid.uuid4().hex}).encode(),
                          {'Content-Type': 'application/json'}))['prompt_id']
    t0 = time.time()
    while True:
        if time.time() - t0 > JOB_LIMIT:
            call('/interrupt', b'{}', {'Content-Type': 'application/json'})
            while pid not in json.loads(call('/history/' + pid)):   # let it stop before the next job
                time.sleep(1)
            free()
            raise RuntimeError('ComfyUI job ran past %d s; interrupted, memory freed' % JOB_LIMIT)
        h = json.loads(call('/history/' + pid))
        if pid in h:
            st = h[pid].get('status', {})
            if st.get('status_str') == 'error':
                raise RuntimeError(json.dumps(st.get('messages'))[:2000])
            imgs = h[pid]['outputs'][save_node]['images']
            q = urllib.parse.urlencode({k: imgs[0][k] for k in ('filename', 'subfolder', 'type')})
            return Image.open(io.BytesIO(call('/view?' + q))).convert('RGB'), time.time() - t0
        time.sleep(0.5)


def seedvr2_graph(name, longer):
    return {
        '1': {'class_type': 'LoadImage', 'inputs': {'image': name}},
        '2': {'class_type': 'ResizeImageMaskNode', 'inputs': {
            'input': ['1', 0], 'resize_type': 'scale longer dimension',
            'resize_type.longer_size': longer, 'scale_method': 'lanczos'}},
        '3': {'class_type': 'SeedVR2Preprocess', 'inputs': {'resized_images': ['2', 0]}},
        '4': {'class_type': 'VAELoader', 'inputs': {'vae_name': 'seedvr2_ema_vae_fp16.safetensors'}},
        '5': {'class_type': 'UNETLoader', 'inputs': {'unet_name': 'seedvr2_7b_int8_convrot.safetensors',
                                                      'weight_dtype': 'default'}},
        '6': {'class_type': 'VAEEncodeTiled', 'inputs': {'pixels': ['3', 0], 'vae': ['4', 0], 'tile_size': 512,
                                                         'overlap': 128, 'temporal_size': 4096, 'temporal_overlap': 8}},
        '7': {'class_type': 'SeedVR2Conditioning', 'inputs': {'model': ['5', 0], 'vae_conditioning': ['6', 0]}},
        '8': {'class_type': 'KSampler', 'inputs': {'model': ['5', 0], 'positive': ['7', 0], 'negative': ['7', 1],
                                                   'latent_image': ['6', 0], 'seed': 42, 'steps': 1, 'cfg': 1.0,
                                                   'sampler_name': 'euler', 'scheduler': 'simple', 'denoise': 1.0}},
        '9': {'class_type': 'VAEDecodeTiled', 'inputs': {'samples': ['8', 0], 'vae': ['4', 0], 'tile_size': 512,
                                                         'overlap': 128, 'temporal_size': 4096, 'temporal_overlap': 8}},
        '10': {'class_type': 'SeedVR2PostProcessing', 'inputs': {'images': ['9', 0], 'original_resized_images': ['2', 0],
                                                                'color_correction_method': 'lab'}},
        '11': {'class_type': 'SaveImage', 'inputs': {'images': ['10', 0], 'filename_prefix': 'texai/seedvr2'}},
    }, '11'


def esrgan_graph(name):
    return {
        '1': {'class_type': 'LoadImage', 'inputs': {'image': name}},
        '2': {'class_type': 'UpscaleModelLoader', 'inputs': {'model_name': 'RealESRGAN_x4plus.safetensors'}},
        '3': {'class_type': 'ImageUpscaleWithModel', 'inputs': {'upscale_model': ['2', 0], 'image': ['1', 0]}},
        '4': {'class_type': 'SaveImage', 'inputs': {'images': ['3', 0], 'filename_prefix': 'texai/esrgan'}},
    }, '4'


def flux2_graph(name, prompt, megapixels=0.25):
    """Reference-guided FLUX.2 Klein 4B candidate, sized for a 12 GB card.

    Real-ESRGAN supplies a clearer reference. The text prompt supplies meaning;
    FLUX can still redraw small symbols or text, so callers must review output.
    The caller's post step restores original alpha, colour and tiling constraints.
    """
    return {
        '1': {'class_type': 'LoadImage', 'inputs': {'image': name}},
        '2': {'class_type': 'UpscaleModelLoader', 'inputs': {'model_name': 'RealESRGAN_x4plus.safetensors'}},
        '3': {'class_type': 'ImageUpscaleWithModel', 'inputs': {'upscale_model': ['2', 0], 'image': ['1', 0]}},
        '4': {'class_type': 'ImageScaleToTotalPixels', 'inputs': {
            'image': ['3', 0], 'upscale_method': 'nearest-exact', 'megapixels': megapixels,
            'resolution_steps': 1}},
        '5': {'class_type': 'GetImageSize', 'inputs': {'image': ['4', 0]}},
        '6': {'class_type': 'UNETLoader', 'inputs': {
            'unet_name': 'flux-2-klein-4b-fp8.safetensors', 'weight_dtype': 'default'}},
        '7': {'class_type': 'CLIPLoader', 'inputs': {
            'clip_name': 'qwen_3_4b_fp4_flux2.safetensors', 'type': 'flux2', 'device': 'default'}},
        '8': {'class_type': 'VAELoader', 'inputs': {'vae_name': 'flux2-vae.safetensors'}},
        '9': {'class_type': 'CLIPTextEncode', 'inputs': {'clip': ['7', 0], 'text': prompt}},
        '10': {'class_type': 'ConditioningZeroOut', 'inputs': {'conditioning': ['9', 0]}},
        '11': {'class_type': 'VAEEncode', 'inputs': {'pixels': ['4', 0], 'vae': ['8', 0]}},
        '12': {'class_type': 'ReferenceLatent', 'inputs': {
            'conditioning': ['9', 0], 'latent': ['11', 0]}},
        '13': {'class_type': 'ReferenceLatent', 'inputs': {
            'conditioning': ['10', 0], 'latent': ['11', 0]}},
        '14': {'class_type': 'CFGGuider', 'inputs': {
            'model': ['6', 0], 'positive': ['12', 0], 'negative': ['13', 0], 'cfg': 1.0}},
        '15': {'class_type': 'RandomNoise', 'inputs': {'noise_seed': 42}},
        '16': {'class_type': 'KSamplerSelect', 'inputs': {'sampler_name': 'euler'}},
        '17': {'class_type': 'Flux2Scheduler', 'inputs': {
            'steps': 4, 'width': ['5', 0], 'height': ['5', 1]}},
        '18': {'class_type': 'EmptyFlux2LatentImage', 'inputs': {
            'width': ['5', 0], 'height': ['5', 1], 'batch_size': 1}},
        '19': {'class_type': 'SamplerCustomAdvanced', 'inputs': {
            'noise': ['15', 0], 'guider': ['14', 0], 'sampler': ['16', 0],
            'sigmas': ['17', 0], 'latent_image': ['18', 0]}},
        '20': {'class_type': 'VAEDecode', 'inputs': {'samples': ['19', 0], 'vae': ['8', 0]}},
        '21': {'class_type': 'SaveImage', 'inputs': {
            'images': ['20', 0], 'filename_prefix': 'texai/flux2-contextual'}},
    }, '21'


def native_input(orig_dir, tex, m):
    """the texture upright, padded PAD texels each side. Transparent texels get
    the key colour when m['keyed'] (as the chat models are sent), otherwise their
    opaque neighbours' colours (texai.bleed): an upscaler blends whatever sits
    next to an edge into it, and the key colour came out as a purple fringe."""
    im = Image.open(os.path.join(orig_dir, tex + '.png')).convert('RGBA')
    if m['flip']:
        im = im.transpose(Image.FLIP_TOP_BOTTOM)
    if m.get('keyed', True):
        bg = Image.new('RGBA', im.size, KEY + (255,))
        bg.alpha_composite(im)
        a = np.asarray(bg.convert('RGB'))
    else:
        import texai
        px = np.asarray(im).astype(np.float64)
        filled = texai.bleed(px[:, :, :3], px[:, :, 3]) if px[:, :, 3].max() > 0 else px[:, :, :3]
        a = np.clip(filled, 0, 255).round().astype(np.uint8)
    a = np.pad(a, ((PAD, PAD), (0, 0), (0, 0)), mode='wrap' if m['wrap'][1] else 'edge')
    a = np.pad(a, ((0, 0), (PAD, PAD), (0, 0)), mode='wrap' if m['wrap'][0] else 'edge')
    return Image.fromarray(a)


def alpha_input(orig_dir, tex, m):
    """the texture's alpha as a grey image, upright and padded as native_input pads
    the colours: an upscaler turns a stair-stepped 1-bit mask into a smooth edge"""
    im = Image.open(os.path.join(orig_dir, tex + '.png')).convert('RGBA')
    if m['flip']:
        im = im.transpose(Image.FLIP_TOP_BOTTOM)
    a = np.asarray(im)[:, :, 3]
    a = np.pad(a, ((PAD, PAD), (0, 0)), mode='wrap' if m['wrap'][1] else 'edge')
    a = np.pad(a, ((0, 0), (PAD, PAD)), mode='wrap' if m['wrap'][0] else 'edge')
    return Image.fromarray(a).convert('RGB')


def framed(answer, m, canvas=1024):
    """crop the padding off and place the texture where prep put it on its 1024 canvas"""
    w, h = m['w'], m['h']
    sx, sy = answer.width / (w + 2 * PAD), answer.height / (h + 2 * PAD)
    tex = answer.crop((round(PAD * sx), round(PAD * sy), round((PAD + w) * sx), round((PAD + h) * sy)))
    b = m['box']
    box = (round(b[0] * canvas), round(b[1] * canvas), round(b[2] * canvas), round(b[3] * canvas))
    out = Image.new('RGB', (canvas, canvas), KEY)
    out.paste(tex.resize((box[2] - box[0], box[3] - box[1]), Image.LANCZOS), box[:2])
    return out


def seedvr2(img, longer, tag):
    graph, save = seedvr2_graph(upload(img, 'texai_%s.png' % tag), int(round(longer / 16)) * 16)
    return run(graph, save)


def esrgan(img, tag):
    graph, save = esrgan_graph(upload(img, 'texai_%s.png' % tag))
    return run(graph, save)


def flux2(img, prompt, tag, megapixels=0.25):
    graph, save = flux2_graph(upload(img, 'texai_flux2_%s.png' % tag), prompt, megapixels)
    return run(graph, save)


TOOLS = ('seedvr2', 'esrgan', 'esrgan-seedvr2')


def main():
    if len(sys.argv) < 4 or sys.argv[3] not in TOOLS:
        print(__doc__)
        return 2
    orig_dir, work, tool = sys.argv[1:4]
    man = json.load(open(os.path.join(work, 'in', 'manifest.json')))
    only = sys.argv[4:]
    out_dir = os.path.join(work, 'out', tool)
    os.makedirs(out_dir, exist_ok=True)
    for tex, m in man.items():
        if only and tex not in only:
            continue
        src = native_input(orig_dir, tex, m)
        # the texture's longer side at REGION pixels, padding included
        final = REGION * max(src.size) / max(m['w'], m['h'])
        if tool == 'esrgan':
            answer, secs = esrgan(src, tex)
        elif tool == 'esrgan-seedvr2':
            # Real-ESRGAN's 4x, then SeedVR2 the rest of the way (about 2x)
            mid, s1 = esrgan(src, tex)
            answer, s2 = seedvr2(mid, final, tex + '_2')
            secs = s1 + s2
        else:
            # SeedVR2 restores about 4x at a time: a 32x jump in one pass only blurs
            mid, s1 = seedvr2(src, 4 * max(src.size), tex)
            answer, s2 = seedvr2(mid, final, tex + '_2')
            secs = s1 + s2
        answer.save(os.path.join(out_dir, tex + '_native.png'))
        framed(answer, m).save(os.path.join(out_dir, tex + '.png'))
        print('%s %s: %dx%d -> %dx%d in %.1f s' % (tool, tex, src.width, src.height, answer.width, answer.height, secs))
    return 0


if __name__ == '__main__':
    sys.exit(main())
