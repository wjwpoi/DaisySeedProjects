"""Check firmware I/O against independently exported KiCad electrical netlists."""
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def nets(board):
    doc=ET.parse(ROOT/f'hardware/reports/{board}-netlist.xml')
    return {n.get('name'):{(p.get('ref'),p.get('pin')) for p in n.findall('node')} for n in doc.findall('.//nets/net')}
class HardwareTests(unittest.TestCase):
    def test_controls_and_display_match_electrical_pins(self):
        wiring=nets('mainboard');source=(ROOT/'firmware/audio/main.cpp').read_text()
        expected={'DISABLE_AUDIO_BYPASS':2,'ENABLE_AUDIO_MUTE':13,'ENCODER_1_A':3,'ENCODER_1_B':4,'ENCODER_1_CLICK':5,
                  'FOOT_SWITCH_1':7,'FOOT_SWITCH_2':6,'LED_1':29,'LED_2':30,
                  'OLED_DISPLAY_SPI_CS':8,'OLED_DISPLAY_SPI_SCK':9,'OLED_DISPLAY_SPI_DC':10,
                  'OLED_DISPLAY_SPI_MOSI':11,'OLED_DISPLAY_SPI_RESET':12}
        for net,pin in expected.items():self.assertIn(('A1',str(pin)),wiring[net])
        adc=re.search(r'knob_pins\[\].*?\{(.*?)\}',source,re.S).group(1)
        for i,d in enumerate(re.findall(r'seed::D(\d+)',adc)):
            self.assertIn(('A1',str(int(d)+7)),wiring[f'POT_{i+1}'])
        patterns=[r'relay.Init\(seed::D1',r'mute.Init\(seed::D12',r'foot\[0\].Init\(seed::D6',r'foot\[1\].Init\(seed::D5',
                  r'encoder.Init\(seed::D3, seed::D2, seed::D4',r'pin_config.dc = seed::D9',r'pin_config.reset = seed::D11']
        for pattern in patterns:self.assertRegex(source,pattern)
    def test_uart_series_resistors_and_filtered_power(self):
        wiring=nets('adapter')
        for net,nodes in {'/SEED_TX':{('J3','2'),('R1','1')},'/ESP_RX':{('J2','4'),('R1','2')},
                          '/ESP_TX':{('J2','3'),('R2','1')},'/SEED_RX':{('J3','3'),('R2','2')}}.items():
            self.assertEqual(wiring[net],nodes)
        self.assertIn(('FB1','1'),wiring['/5V_SOURCE']);self.assertIn(('FB1','2'),wiring['/5V_WIFI'])
        self.assertIn(('J2','1'),wiring['/5V_WIFI'])
        for ref in ('C1','C2','C3'):self.assertIn((ref,'2'),wiring['/GND'])
    def test_monophonic_output_avoids_trs_phase_cancellation(self):
        source=(ROOT/'firmware/audio/main.cpp').read_text()
        self.assertRegex(source,r'out\[1\]\[i\] = 0.0f;')
        self.assertRegex(source,r'audio.process\(in\[0\], out\[0\], size\)')
if __name__=='__main__':unittest.main()
