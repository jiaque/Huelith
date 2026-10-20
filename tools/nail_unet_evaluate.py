"""Evaluate MIT-declared NailVirtuoso weights using tensor-only loading.

Architecture matches nngeek195/nail-segmentation-v1 Running_Stage/app.py.
This is an evaluation tool; no model is added to the product package.
"""
from pathlib import Path
import hashlib,json,time
import torch
from torch import nn
import numpy as np
from PIL import Image
import cv2

class DoubleConv(nn.Module):
    def __init__(self,a,b):
        super().__init__();self.conv=nn.Sequential(nn.Conv2d(a,b,3,padding=1,bias=False),nn.BatchNorm2d(b),nn.ReLU(),nn.Conv2d(b,b,3,padding=1,bias=False),nn.BatchNorm2d(b),nn.ReLU())
    def forward(self,x):return self.conv(x)

class UNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.dconv_down1=DoubleConv(3,32);self.dconv_down2=DoubleConv(32,64);self.dconv_down3=DoubleConv(64,128);self.dconv_down4=DoubleConv(128,256)
        self.maxpool=nn.MaxPool2d(2);self.up3=nn.ConvTranspose2d(256,128,2,stride=2);self.up2=nn.ConvTranspose2d(128,64,2,stride=2);self.up1=nn.ConvTranspose2d(64,32,2,stride=2)
        self.dconv_up3=DoubleConv(256,128);self.dconv_up2=DoubleConv(128,64);self.dconv_up1=DoubleConv(64,32);self.conv_last=nn.Conv2d(32,1,1)
    def forward(self,x):
        a=self.dconv_down1(x);b=self.dconv_down2(self.maxpool(a));c=self.dconv_down3(self.maxpool(b));d=self.dconv_down4(self.maxpool(c))
        d=self.dconv_up3(torch.cat([self.up3(d),c],1));d=self.dconv_up2(torch.cat([self.up2(d),b],1));d=self.dconv_up1(torch.cat([self.up1(d),a],1));return torch.sigmoid(self.conv_last(d))

def main():
    root=Path(__file__).resolve().parents[1];data=root/'build/nail-evaluation';model=data/'mit-unet.pt';digest=hashlib.sha256(model.read_bytes()).hexdigest()
    assert digest=='69b6a986b5fa67447fc64b508012894b582a4969664a579c5890902b1a3bc198'
    torch.set_num_threads(4);net=UNet();net.load_state_dict(torch.load(model,map_location='cpu',weights_only=True));net.eval()
    out=root/'docs/test-evidence/M3/nail-model-evaluation';records=[]
    for path in [data/'hand-1.jpg',data/'hand-2.jpg',root/'docs/test-evidence/M3/quality/leroy-chiao-source.png']:
        image=Image.open(path).convert('RGB');x=torch.from_numpy(np.array(image.resize((128,128),Image.Resampling.BILINEAR)).transpose(2,0,1).copy()).float()[None]/127.5-1
        start=time.perf_counter()
        with torch.no_grad():prob=net(x)[0,0].numpy()
        elapsed=(time.perf_counter()-start)*1000;mask=cv2.resize(prob,image.size)>.5;overlay=np.array(image);overlay[mask]=overlay[mask]*.6+np.array([30,230,180])*.4
        Image.fromarray(overlay).save(out/(path.stem+'-unet.jpg'));Image.fromarray(np.uint8(mask)*255).save(out/(path.stem+'-unet-mask.png'))
        n,_,stats,_=cv2.connectedComponentsWithStats(np.uint8(mask));record={'file':path.name,'components':int(sum(stats[1:,4]>8)),'pixels':int(mask.sum()),'inferenceMs':elapsed};records.append(record);print(record)
    (out/'unet-results.json').write_text(json.dumps({'sha256':digest,'cases':records},indent=2),encoding='utf-8')
if __name__=='__main__':main()
