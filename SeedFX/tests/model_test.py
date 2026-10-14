import copy,importlib.util,json,math,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('model_tool',ROOT/'tools/model_tool.py');tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
def example():
    groups=[]
    for start,end,k in [(0,14,6),(14,16,15),(16,23,6)]:groups.append(dict(channels=3,kernel_size=k,dilations=tool.DILATIONS[start:end],activation='LeakyReLU',head_size=3))
    return dict(architecture='WaveNet',sample_rate=48000,config=dict(layers=groups,head=dict(in_channels=3,out_channels=1,kernel_size=16,bias=True)),weights=[0.0]*1871)
class ModelTests(unittest.TestCase):
    def test_exact_layout(self):self.assertEqual(len(tool.validate(example())),1871)
    def test_bad_weight_count(self):
        m=example();m['weights'].pop()
        with self.assertRaises(ValueError):tool.validate(m)
    def test_nonfinite(self):
        m=example();m['weights'][100]=float('nan')
        with self.assertRaises(ValueError):tool.validate(m)
    def test_same_size_wrong_topology(self):
        m=example();m['config']['layers'][0]['dilations'][0]=2
        with self.assertRaises(ValueError):tool.validate(m)
    def test_wrong_activation(self):
        m=example();m['config']['layers'][0]['activation']='Tanh'
        with self.assertRaises(ValueError):tool.validate(m)
    def test_container_sample_rate(self):
        sub=example();sub.pop('sample_rate')
        container=dict(architecture='SlimmableContainer',sample_rate=48000,config=dict(submodels=[dict(max_value=0.5,model=sub)]))
        self.assertEqual(len(tool.validate(container)),1871)
        container['config']['submodels'][0]['max_value']=1
        with self.assertRaises(ValueError):tool.validate(container)
    def test_wrong_head(self):
        m=example();m['config']['head']['kernel_size']=8
        with self.assertRaises(ValueError):tool.validate(m)
    def test_fp32_overflow(self):
        m=example();m['weights'][0]=1e100
        with self.assertRaises(ValueError):tool.validate(m)
    def test_wrong_sample_rate(self):
        m=example();m['sample_rate']=44100
        with self.assertRaises(ValueError):tool.validate(m)
if __name__=='__main__':unittest.main()
