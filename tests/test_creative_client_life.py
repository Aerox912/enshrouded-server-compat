import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import verify_creative_client_life as life
from verify_creative_flight_offsets import CLIENT_BUILD,VerificationError

class ImageFixture:
    path='<synthetic client-life layout>'
    def __init__(self):
        self.sha256=CLIENT_BUILD['sha256'];self.timestamp=CLIENT_BUILD['timestamp']
        self.image_base=CLIENT_BUILD['image_base'];self.image_size=CLIENT_BUILD['image_size']
        self.bytes={s['rva']:bytes.fromhex(s['signature']) for s in life.ANCHORS}
        self.ranges={s['rva']:s['function'] for s in life.ANCHORS}
        self.words={
            0x1CAD2A0+8*8:self.image_base+0x78CD00,
            life.BASE_ACTOR_DESCRIPTOR_RVA:self.image_base+life.BASE_ACTOR_NAME_RVA,
            life.BASE_ACTOR_DESCRIPTOR_RVA+8:len('BaseActor'),
            life.BASE_ACTOR_DESCRIPTOR_RVA+0x20:self.image_base+life.BASE_ACTOR_QUALIFIED_NAME_RVA,
            life.BASE_ACTOR_DESCRIPTOR_RVA+0x28:len('keen::ecs::BaseActor'),
            life.BASE_ACTOR_DESCRIPTOR_RVA+0x40:0x8000800000C20,
            life.GENERIC_ACTOR_DESCRIPTOR_RVA:self.image_base+life.GENERIC_ACTOR_NAME_RVA,
            life.GENERIC_ACTOR_DESCRIPTOR_RVA+8:len('Actor'),
            life.GENERIC_ACTOR_DESCRIPTOR_RVA+0x20:self.image_base+life.GENERIC_ACTOR_QUALIFIED_NAME_RVA,
            life.GENERIC_ACTOR_DESCRIPTOR_RVA+0x28:len('keen::ecs::Actor'),
            life.GENERIC_ACTOR_DESCRIPTOR_RVA+0x40:0x8000800000E10,
            life.CLIENT_ACTOR_DESCRIPTOR_RVA:self.image_base+life.CLIENT_ACTOR_NAME_RVA,
            life.CLIENT_ACTOR_DESCRIPTOR_RVA+8:len('ClientActor'),
            life.CLIENT_ACTOR_DESCRIPTOR_RVA+0x20:self.image_base+life.CLIENT_ACTOR_QUALIFIED_NAME_RVA,
            life.CLIENT_ACTOR_DESCRIPTOR_RVA+0x28:len('keen::ecs::ClientActor'),
            life.CLIENT_ACTOR_DESCRIPTOR_RVA+life.CLIENT_ACTOR_BASE_DESCRIPTOR_OFFSET:self.image_base+life.BASE_ACTOR_DESCRIPTOR_RVA,
            life.CLIENT_ACTOR_DESCRIPTOR_RVA+0x40:0x8000800000C38,
            life.UI_PLAYER_STATE_DESCRIPTOR_RVA:self.image_base+life.CLIENT_ACTOR_NAME_RVA,
            life.UI_PLAYER_STATE_DESCRIPTOR_RVA+8:len('ClientActor'),
            life.UI_PLAYER_STATE_DESCRIPTOR_RVA+0x48:self.image_base+life.UI_PLAYER_STATE_NAME_RVA,
            life.UI_PLAYER_STATE_DESCRIPTOR_RVA+0x50:len('update_ui_player_state'),
            life.UI_PLAYER_STATE_DESCRIPTOR_RVA+0x58:self.image_base+life.UI_PLAYER_STATE_CALLBACK_RVA,
        }
        self.strings={
            life.BASE_ACTOR_NAME_RVA:'BaseActor',
            life.BASE_ACTOR_QUALIFIED_NAME_RVA:'keen::ecs::BaseActor',
            life.GENERIC_ACTOR_NAME_RVA:'Actor',
            life.GENERIC_ACTOR_QUALIFIED_NAME_RVA:'keen::ecs::Actor',
            life.CLIENT_ACTOR_NAME_RVA:'ClientActor',
            life.CLIENT_ACTOR_QUALIFIED_NAME_RVA:'keen::ecs::ClientActor',
            life.UI_PLAYER_STATE_NAME_RVA:'update_ui_player_state',
        }
        self.words.update({
            life.STATE_FLAG_DESCRIPTOR_RVA+life.STATE_FLAG_QUALIFIED_NAME_POINTER_OFFSET:self.image_base+0x1CA2CF0,
            life.STATE_FLAG_DESCRIPTOR_RVA+life.STATE_FLAG_FIELD_COUNT_OFFSET:0xC0000003E,
            life.STATE_FLAG_DESCRIPTOR_RVA+life.STATE_FLAG_ENUM_TABLE_POINTER_OFFSET:self.image_base+life.STATE_FLAG_ENUM_TABLE_RVA,
            life.STATE_FLAG_ENUM_TABLE_RVA+7*life.STATE_FLAG_ENTRY_STRIDE:self.image_base+0x1CA0AC0,
            life.STATE_FLAG_ENUM_TABLE_RVA+7*life.STATE_FLAG_ENTRY_STRIDE+0x10:7,
            life.STATE_FLAG_ENUM_TABLE_RVA+12*life.STATE_FLAG_ENTRY_STRIDE:self.image_base+0x1CA0CF0,
            life.STATE_FLAG_ENUM_TABLE_RVA+12*life.STATE_FLAG_ENTRY_STRIDE+0x10:12,
        })
        self.strings.update({0x1CA2CF0:'keen::actor::StateFlag',0x1CA0AC0:'Dead',0x1CA0CF0:'Spawning'})
    def code_bytes(self,rva,size):return self.bytes[rva][:size]
    def functions_at(self,rva):
        value=self.ranges[rva]
        return [SimpleNamespace(begin=value[0],end=value[1])] if value else []
    def function_at(self,rva):
        matches=self.functions_at(rva)
        if len(matches)!=1:raise VerificationError('missing unwind')
        return matches[0]
    def qword_at(self,rva):return self.words[rva]
    def c_string_at(self,rva):
        try:return self.strings[rva]
        except KeyError as error:raise VerificationError('missing synthetic string') from error
    def manifest(self):return {}

class LifeLayoutTests(unittest.TestCase):
    def verify(self,image):
        with patch.object(life,'verify_client_ownership',return_value={}):
            return life.verify_image(image)
    def test_complete_instruction_anchors(self):
        self.assertEqual(len(life.ANCHORS),25)
        self.assertEqual(len({x['rva'] for x in life.ANCHORS}),25)
        for anchor in life.ANCHORS:
            self.assertEqual(len(bytes.fromhex(anchor['signature'])),sum(anchor['instruction_lengths']))
    def test_synthetic_supported_layout(self):
        result=self.verify(ImageFixture());self.assertTrue(result['static_only'])
        self.assertFalse(result['runtime_installed']);self.assertFalse(result['live_gameplay_verified'])
        typed=result['client_actor']
        self.assertEqual(typed['client_actor']['qualified_name'],'keen::ecs::ClientActor')
        self.assertEqual(typed['base_actor']['qualified_name'],'keen::ecs::BaseActor')
        self.assertEqual(typed['client_actor_inheritance']['parent_size'],'0xC20')
        self.assertEqual(typed['client_actor_inheritance']['client_size'],'0xC38')
        self.assertEqual(typed['generic_actor']['qualified_name'],'keen::ecs::Actor')
        consumer=typed['state_consumer']
        self.assertEqual(consumer['first_component'],'keen::ecs::ClientActor')
        self.assertEqual(consumer['callback_rva'],'0x2afef0')
        self.assertEqual(consumer['state_bits'],(0x25,0x21))
    def test_every_changed_instruction_anchor_rejected(self):
        for anchor in life.ANCHORS:
            with self.subTest(name=anchor['name']):
                image=ImageFixture();value=bytearray(image.bytes[anchor['rva']]);value[0]^=1;image.bytes[anchor['rva']]=bytes(value)
                with self.assertRaises(VerificationError):self.verify(image)
    def test_changed_unwind_and_new_leaf_unwind_rejected(self):
        for anchor in life.ANCHORS:
            with self.subTest(name=anchor['name']):
                image=ImageFixture();value=image.ranges[anchor['rva']]
                image.ranges[anchor['rva']]=(value[0],value[1]+1) if value else (anchor['rva'],anchor['rva']+256)
                with self.assertRaises(VerificationError):self.verify(image)
    def test_wrong_binary_identity_rejected_before_layout(self):
        for field in ('sha256','timestamp','image_base','image_size'):
            with self.subTest(field=field):
                image=ImageFixture();setattr(image,field,'0'*64 if field=='sha256' else getattr(image,field)+1)
                with self.assertRaises(VerificationError):self.verify(image)
    def test_metadata_pointer_or_size_drift_rejected(self):
        for at in ImageFixture().words:
            with self.subTest(rva=at):
                image=ImageFixture();image.words[at]+=1
                with self.assertRaises(VerificationError):self.verify(image)
    def test_client_dead_and_spawning_enum_values_are_verified(self):
        result=self.verify(ImageFixture())
        state_flag=result['state_flag_enum']
        self.assertEqual(state_flag['qualified_name'],'keen::actor::StateFlag')
        self.assertEqual(state_flag['field_count'],62)
        self.assertEqual(state_flag['entries']['Dead']['value'],7)
        self.assertEqual(state_flag['entries']['Spawning']['value'],12)
        self.assertEqual(result['reader']['dead_bit'],7)
        self.assertEqual(result['reader']['spawning_bit'],12)
        for name in ('Dead','Spawning'):
            with self.subTest(name=name):
                image=ImageFixture()
                value_rva=int(state_flag['entries'][name]['value_rva'],16)
                image.words[value_rva]+=1
                with self.assertRaises(VerificationError):self.verify(image)
    def test_exact_pinned_actor_type_names_required(self):
        for at in ImageFixture().strings:
            with self.subTest(rva=at):
                image=ImageFixture();image.strings[at]='OtherActor'
                with self.assertRaises(VerificationError):self.verify(image)

if __name__=='__main__':unittest.main()
