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


def run(graph, save_node):
    pid = json.loads(call('/prompt', json.dumps({'prompt': graph, 'client_id': uuid.uuid4().hex}).encode(),
                          {'Content-Type': 'application/json'}))['prompt_id']
    t0 = time.time()
    while True:
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
