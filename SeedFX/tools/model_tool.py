#!/usr/bin/env python3
"""Validate the exact supported A2 topology before exporting a .nam model."""
import argparse,json,math,re,sys
from pathlib import Path
KERNELS=[6]*14+[15,15]+[6]*7
DILATIONS=[1,3,7,17,41,101,239,1,3,7,17,41,101,239,1,13,1,3,7,17,41,101,239]
def validate(model):
    if model.get('architecture')=='SlimmableContainer':
        candidates=model.get('config',{}).get('submodels',[])
        if not candidates:raise ValueError('SlimmableContainer has no submodels')
        selected=[x for x in candidates if x.get('max_value')==0.5]
        if len(selected)!=1:raise ValueError('Require exactly one max_value=0.5 submodel')
        outer_rate=model.get('sample_rate')
        model=dict(selected[0]['model'])
        if outer_rate is not None and model.get('sample_rate',outer_rate)!=outer_rate:
            raise ValueError('Container/submodel sample rates conflict')
        model.setdefault('sample_rate',outer_rate)
    if model.get('architecture')!='WaveNet':raise ValueError('Only the fixed A2 WaveNet architecture is supported')
    config=model.get('config',{})
    layers=config.get('layers')
    if not isinstance(layers,list):raise ValueError('Missing layers: weight count alone does not prove compatibility')
    kernels=[];dilations=[]
    for layer in layers:
        if layer.get('channels')!=3:raise ValueError('Every layer group must use 3 channels')
        ds=layer.get('dilations')
        if not isinstance(ds,list):raise ValueError('Missing dilation schedule')
        k=layer.get('kernel_size')
        if isinstance(k,int):ks=[k]*len(ds)
        elif isinstance(k,list) and len(k)==len(ds):ks=k
        else:raise ValueError('Unsupported kernel schedule')
        kernels+=ks;dilations+=ds
        activation=layer.get('activation')
        if activation not in ('LeakyReLU', 'LeakyReLU(negative_slope=0.01)'):
            raise ValueError('Require LeakyReLU with slope 0.01; check the exporter schema explicitly')
        if layer.get('input_size',3)!=3 and len(kernels)>len(ds):raise ValueError('Unsupported intermediate input size')
        if layer.get('head_size',3)!=3:raise ValueError('Unsupported head size')
        if layer.get('gated',False) or layer.get('condition_size',1)!=1:raise ValueError('Gated/conditioned models are unsupported')
    if kernels!=KERNELS or dilations!=DILATIONS:raise ValueError('Kernel/dilation topology differs from the compiled runtime')
    head=config.get('head')
    if not isinstance(head,dict) or any(head.get(key)!=value for key,value in
            {'in_channels':3,'out_channels':1,'kernel_size':16,'bias':True}.items()):
        raise ValueError('Require explicit single linear 3-to-1, 16-tap biased head metadata')
    if head.get('activation') not in (None,'Identity','Linear') or head.get('num_layers',1)!=1:
        raise ValueError('Unsupported output head')
    weights=model.get('weights')
    if not isinstance(weights,list) or len(weights)!=1871:raise ValueError('Require exactly 1871 weights')
    if not all(isinstance(w,(int,float)) and not isinstance(w,bool) and math.isfinite(w) and abs(w)<=3.402823466e38 for w in weights):raise ValueError('Weights must be finite numbers')
    sample_rate=model.get('sample_rate')
    if sample_rate!=48000:raise ValueError('Require explicit 48000 Hz sample_rate metadata')
    return weights
def verify_builtin():
    source=Path(__file__).resolve().parents[2]/'Software/GuitarPedal/Effect-Modules/Nam/model_data_nam_a2.h'
    arrays=re.findall(r'^inline constexpr float (\w+)\[nam_a2_daisy::kA2WeightCount\]\s*=\s*\{([^}]+)\}',source.read_text(),re.MULTILINE)
    for name,data in arrays:
        values=[float(s.strip().removesuffix('f')) for s in data.split(',') if s.strip()]
        if len(values)!=1871 or not all(math.isfinite(x) for x in values):raise ValueError(f'Invalid built-in model {name}')
    if len(arrays)!=5:raise ValueError('Expected the five pinned reference models')
    print('Verified 5 built-in model arrays: 1871 finite weights each')
def main():
    p=argparse.ArgumentParser();p.add_argument('model',nargs='?',type=Path);p.add_argument('--verify-builtin',action='store_true');p.add_argument('--output',type=Path)
    args=p.parse_args()
    if args.verify_builtin:verify_builtin();return
    if not args.model:p.error('provide a .nam model or --verify-builtin')
    weights=validate(json.loads(args.model.read_text()))
    content='// Validated fixed-topology 48 kHz A2 model.\ninline constexpr float kUserModel[1871] = {\n'
    content+='\n'.join('    '+', '.join(f'{x:.9e}f' for x in weights[i:i+8])+',' for i in range(0,len(weights),8))+'\n};\n'
    if args.output:
        with args.output.open('x') as f:f.write(content)
    else:sys.stdout.write(content)
if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,OSError) as e:print(f'Model rejected: {e}',file=sys.stderr);sys.exit(1)
