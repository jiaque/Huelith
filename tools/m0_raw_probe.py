"""Decode real Bayer RAW and run full-resolution tiled M0 inference.

Outputs remain research assets. No product RAW editor or M1 import is added.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time

import numpy as np
import onnxruntime as ort
import rawpy
from PIL import Image


def pack(raw):
    image=raw.raw_image_visible.astype(np.float32)
    pattern=raw.raw_pattern; colors=raw.color_desc.decode('ascii').rstrip('\0')
    if pattern.shape!=(2,2) or image.shape[0]%2 or image.shape[1]%2: raise ValueError('even Bayer image required')
    channels=[]; used=set()
    for letter in 'RGGB':
        y,x=next((y,x) for y in range(2) for x in range(2) if colors[int(pattern[y,x])]==letter and (y,x) not in used)
        used.add((y,x)); black=raw.black_level_per_channel[int(pattern[y,x])]
        channels.append(np.clip((image[y::2,x::2]-black)/(raw.white_level-black),0,1))
    return np.stack(channels)


def tiled(session,packed,progress=True,stop_after=None):
    _,height,width=packed.shape
    halo=128; core=256; tile=512
    padded=np.pad(packed,((0,0),(halo,halo+core),(halo,halo+core)),mode='reflect')
    output=np.empty((height*2,width*2,3),np.float32); times=[]
    for y in range(0,height,core):
        for x in range(0,width,core):
            if stop_after is not None and len(times)>=stop_after: return None,times
            array=np.ascontiguousarray(padded[None,:,y:y+tile,x:x+tile])
            start=time.perf_counter(); result=session.run(None,{'input':array})[0][0].transpose(1,2,0);times.append(time.perf_counter()-start)
            if not np.isfinite(result).all(): raise ValueError('nonfinite inference result')
            h=min(core,height-y)*2;w=min(core,width-x)*2
            output[y*2:y*2+h,x*2:x*2+w]=result[halo*2:halo*2+h,halo*2:halo*2+w]
        if progress: print('completed rows',min(y+core,height)*2,'/',height*2,flush=True)
    return output,times


def preview(rgb):
    positive=np.maximum(rgb,0)
    # Explicit preview-only global exposure and highlight rolloff, not camera rendering parity.
    scale=.6/max(float(np.quantile(positive,.99)),1e-6)
    mapped=positive*scale; mapped=mapped/(1+mapped)
    srgb=np.where(mapped<=.0031308,12.92*mapped,1.055*mapped**(1/2.4)-.055)
    return np.uint8(np.clip(srgb,0,1)*255+.5)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--raw',type=Path,required=True)
    parser.add_argument('--model',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--provider',choices=['cpu','dml'],default='cpu')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    start=time.perf_counter()
    with rawpy.imread(str(args.raw)) as raw:
        packed=pack(raw); matrix=np.asarray(raw.rgb_xyz_matrix[:3,:],np.float64)
        report=dict(raw=args.raw.name,sha256=hashlib.sha256(args.raw.read_bytes()).hexdigest(),
            libraw=list(rawpy.libraw_version),rawpy=rawpy.__version__,visible=[raw.sizes.height,raw.sizes.width],
            pattern=raw.raw_pattern.tolist(),black=raw.black_level_per_channel,white=raw.white_level,
            source_orientation=raw.sizes.flip,wb=raw.camera_whitebalance,matrix=matrix.tolist())
        # Only a reference preview; does not enter the denoising path.
        baseline=raw.postprocess(use_camera_wb=True,no_auto_bright=True,output_bps=8)
        Image.fromarray(baseline).resize((1200,800)).save(args.output/'libraw-preview.jpg',quality=95)
    report['decode_seconds']=time.perf_counter()-start
    options=ort.SessionOptions();options.intra_op_num_threads=4;options.inter_op_num_threads=1
    providers=['CPUExecutionProvider']
    if args.provider=='dml':
        options.enable_mem_pattern=False;options.execution_mode=ort.ExecutionMode.ORT_SEQUENTIAL
        options.add_session_config_entry('session.disable_cpu_ep_fallback','1')
        providers=[('DmlExecutionProvider',{'device_id':0})]
    session=ort.InferenceSession(str(args.model),options,providers=providers);session.disable_fallback()
    output,times=tiled(session,packed)
    # One gain across the completed image; matching each tile separately would create exposure seams.
    gain=float(np.mean(packed,dtype=np.float64)/np.mean(output,dtype=np.float64))
    output*=gain
    # Follow pinned upstream Bayer demo: camera RGB -> XYZ D65 -> linear Rec.2020.
    xyz_to_rec2020=np.array([[1.716651188,-.355670784,-.253366281],[-.666684352,1.616481237,.015768546],[.017639857,-.042770613,.942103121]])
    rgb=np.einsum('...c,dc->...d',output,(xyz_to_rec2020@np.linalg.inv(matrix)).astype(np.float32))
    np.save(args.output/'linear-rec2020.npy',rgb)
    xyz_to_srgb=np.array([[3.2404542,-1.5371385,-.4985314],[-.969266,1.8760108,.041556],[.0556434,-.2040259,1.0572252]])
    srgb_linear=np.einsum('...c,dc->...d',output,(xyz_to_srgb@np.linalg.inv(matrix)).astype(np.float32))
    rendered=preview(srgb_linear)
    Image.fromarray(rendered).resize((1200,800)).save(args.output/'denoised-preview.jpg',quality=95)
    h,w=rendered.shape[:2];Image.fromarray(rendered[h//2-512:h//2+512,w//2-512:w//2+512]).save(args.output/'center-100percent.png')
    cancelled,partial_times=tiled(session,packed[:,:512,:512],False,stop_after=1)
    report.update(onnxruntime=ort.__version__,requested_provider=args.provider,providers=session.get_providers(),model_sha256=hashlib.sha256(args.model.read_bytes()).hexdigest(),
        tiles=len(times),tile_seconds_sum=sum(times),tile_seconds_p95=float(np.quantile(times,.95)),
        gain=gain,output_shape=list(rgb.shape),finite=bool(np.isfinite(rgb).all()),
        output_min=float(rgb.min()),output_max=float(rgb.max()),
        cancellation_discards_partial=cancelled is None,cancel_granularity_seconds=partial_times[-1],
        limitations=['No clean reference: no absolute denoising PSNR claimed.',
        'Upstream color conversion is a candidate; white balance/DCP and camera color accuracy not approved.',
        'Preview tone mapping differs from LibRaw baseline, so previews are not a controlled visual denoising comparison.'])
    (args.output/'raw-results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report),flush=True)


if __name__=='__main__':main()
