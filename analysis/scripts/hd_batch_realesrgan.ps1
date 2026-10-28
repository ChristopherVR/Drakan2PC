# Batch-upscale a gsr texture dump with Real-ESRGAN ncnn Vulkan (RTX GPU) and split RGB / alpha so that constant-alpha textures keep their colours.
# usage: powershell -File hd_batch_realesrgan.ps1 -Dump textures\dump -Out textures -Exe C:\tools\realesrgan\realesrgan-ncnn-vulkan.exe [-Model realesrgan-x4plus] [-Scale 4]
# Real-ESRGAN (xinntao, BSD-3) ncnn build: https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/releases (models: realesrgan-x4plus, realesrgan-x4plus-anime, realesr-animevideov3).
# The file names (16 hex digit hash) MUST be kept. Textures with a constant alpha channel (all 0 = CT24 tiles with TEXA 0, or all 255) are processed as RGB and the constant alpha is re-added.
param([string]$Dump="textures\dump",[string]$Out="textures",[string]$Exe="realesrgan-ncnn-vulkan.exe",[string]$Model="realesrgan-x4plus",[int]$Scale=4)
$tmp = Join-Path $env:TEMP "drakan_hd_rgb"; New-Item -ItemType Directory -Force $tmp,"$tmplpha",$Out | Out-Null
py -3 -I -c @"
import sys,glob,os
from PIL import Image
import numpy as np
src,tmp,out=sys.argv[1:4]
for p in glob.glob(os.path.join(src,'*.png')):
    n=os.path.basename(p)
    if os.path.exists(os.path.join(out,n)): continue          # resume support
    a=np.asarray(Image.open(p).convert('RGBA'))
    Image.fromarray(a[...,:3],'RGB').save(os.path.join(tmp,n))
    # alpha plane saved separately (upscaled bicubic later when not constant)
    Image.fromarray(a[...,3],'L').save(os.path.join(tmp,'alpha',n))
"@ $Dump $tmp $Out
& $Exe -i $tmp -o "$tmp\up" -n $Model -s $Scale -f png -g 0 -j 2:2:2
py -3 -I -c @"
import sys,glob,os
from PIL import Image
import numpy as np
tmp,out,sc=sys.argv[1],sys.argv[2],int(sys.argv[3])
for p in glob.glob(os.path.join(tmp,'up','*.png')):
    n=os.path.basename(p)
    rgb=Image.open(p).convert('RGB'); w,h=rgb.size
    al=Image.open(os.path.join(tmp,'alpha',n)); a=np.asarray(al)
    A=al.resize((w,h),Image.BICUBIC) if a.min()!=a.max() else Image.new('L',(w,h),int(a[0,0]))
    rgb.putalpha(A); rgb.save(os.path.join(out,n),optimize=True)
"@ $tmp $Out $Scale
"done: $((Get-ChildItem $Out -Filter *.png).Count) files in $Out"
