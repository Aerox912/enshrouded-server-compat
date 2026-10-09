"""Verify pinned CLIENT current-world/typed ClientActor life reader layout.
Usage: python verify_creative_client_life.py --client <enshrouded.exe> [--json <evidence.json>]
Read-only: full hash, PE identity, unwind ranges, small instruction signatures
and metadata. No runtime pointers, memory, hooks or binary changes.
"""
import argparse
import json
from pathlib import Path
from verify_creative_flight_offsets import (PEImage, CLIENT_BUILD, VerificationError,
    require_build, verify_site, verify_client_ownership)

STATE_FLAG_DESCRIPTOR_RVA = 0x1B2D230
STATE_FLAG_QUALIFIED_NAME_POINTER_OFFSET = 0x20
STATE_FLAG_FIELD_COUNT_OFFSET = 0x48
STATE_FLAG_ENUM_TABLE_POINTER_OFFSET = 0x60
STATE_FLAG_ENUM_TABLE_RVA = 0x1B23720
STATE_FLAG_ENUM_COUNT = 62
STATE_FLAG_ENTRY_STRIDE = 0x28
STATE_FLAG_EXPECTED_ENTRIES = {"Dead": (7, 7), "Spawning": (12, 12)}

ANCHORS = ({'name': 'inline-game-object',
  'rva': 7806086,
  'function': (7806080, 7806334),
  'signature': '488D0D3360790141B8C8030000E8C808B300488D0D21607901E8AC490B00',
  'instruction_lengths': (7, 6, 5, 7, 5)},
 {'name': 'inline-game-getter',
  'rva': 7834704,
  'function': None,
  'signature': '488D0569F07801C3',
  'instruction_lengths': (7, 1)},
 {'name': 'game-client-read',
  'rva': 7916839,
  'function': (7916800, 7916917),
  'signature': '488B8B500200004885C97437',
  'instruction_lengths': (7, 3, 2)},
 {'name': 'game-client-publish',
  'rva': 7898720,
  'function': (7898720, 7898758),
  'signature': '488BC8E8A8D3FDFF4C8BF0488D55804C89B750020000',
  'instruction_lengths': (3, 5, 3, 4, 7)},
 {'name': 'client-session-publish',
  'rva': 7818274,
  'function': (7818269, 7818298),
  'signature': '4889878828050048897828',
  'instruction_lengths': (7, 4)},
 {'name': 'active-session-scene',
  'rva': 7907486,
  'function': (7906832, 7908008),
  'signature': '488B87882805004885C074378078200074314C8B80800100004D85C07425',
  'instruction_lengths': (7, 3, 2, 4, 2, 7, 3, 2)},
 {'name': 'scene-simulation',
  'rva': 7803456,
  'function': None,
  'signature': '488B81882805004885C07457488B80800100004885C0744B4883B8C0393400007441',
  'instruction_lengths': (7, 3, 2, 7, 3, 2, 8, 2)},
 {'name': 'scene-world-constructor-input',
  'rva': 7849299,
  'function': (7847840, 7850087),
  'signature': '488B87C00100000F1087F0273300488985C8000000',
  'instruction_lengths': (7, 7, 7)},
 {'name': 'scene-simulation-publish',
  'rva': 7849705,
  'function': (7847840, 7850087),
  'signature': '488D4D40E83E87BAFF488B8F803A3400488987C0393400',
  'instruction_lengths': (4, 5, 7, 7)},
 {'name': 'simulation-constructor-arguments',
  'rva': 3296844,
  'function': (3296816, 3305322),
  'signature': '4C8BE133D2488B09E867DCFFFF4C8BE8',
  'instruction_lengths': (3, 2, 3, 5, 3)},
 {'name': 'simulation-world-copy',
  'rva': 3296887,
  'function': (3296816, 3305322),
  'signature': '498B842488000000488BCB49894508',
  'instruction_lengths': (8, 3, 4)},
 {'name': 'world-own-component-registry',
  'rva': 9013426,
  'function': (9013312, 9021000),
  'signature': '498D962809000049894E08',
  'instruction_lengths': (7, 4)},
 {'name': 'world-own-registry-publish',
  'rva': 9013590,
  'function': (9013312, 9021000),
  'signature': '4989961842CC00',
  'instruction_lengths': (7,)},
 {'name': 'component-array-count-stride',
  'rva': 9141544,
  'function': (9140416, 9142422),
  'signature': '488BB7380900004C8BB728090000488B9F300900004885F6742A4883C358488B134885D27411498B064C8D4540498BCEC6454000FF50104881C3000100004883EE0175DA',
  'instruction_lengths': (7, 7, 7, 3, 2, 4, 3, 3, 2, 3, 4, 3, 4, 3, 7, 4, 2)},
 {'name': 'component-index-and-reflection',
  'rva': 9389870,
  'function': (9389632, 9390158),
  'signature': '0FB71048C1E208480393300900004883BA80000000000F84990000004C8B42280FB68B2042CC004983C020',
  'instruction_lengths': (3, 4, 7, 8, 6, 4, 7, 4)},
 {'name': 'root-component-resolver',
  'rva': 9205279,
  'function': (9205264, 9205353),
  'signature': '488DBA1842CC004489442440488BF1488D542440488B0F418BD94881C188000000E8CBE7FFFF4C8BC0448BCB488BD7',
  'instruction_lengths': (7, 5, 3, 5, 3, 3, 7, 5, 3, 3, 3)},
 {'name': 'entity-map-hash',
  'rva': 9212906,
  'function': (9212896, 9213050),
  'signature': '837954004C8BC94C8B591074758B1A8BC3498B7908C1E81033C369D03B9F5D048BC2C1E81033C269C03B9F5D048BC848C1E8104833C1498D4BFF4823C1',
  'instruction_lengths': (4, 3, 4, 2, 2, 2, 4, 3, 2, 6, 2, 3, 2, 6, 2, 4, 3, 4, 3)},
 {'name': 'entity-map-buckets',
  'rva': 9212967,
  'function': (9212896, 9213050),
  'signature': '488BD00FB6C848C1EA064C8B04D749D3E841F6C001742E4D8B512841391C827427488D480133C0493BCB480F42C1488BD00FB6C848C1EA064C8B04D749D3E841F6C00175D6498BC3488B5C2408488B7C2410C3',
  'instruction_lengths': (3, 3, 4, 4, 3, 4, 2, 4, 4, 2, 4, 2, 3, 4, 3, 3, 4, 4, 3, 4, 2, 3, 5, 5, 1)},
 {'name': 'component-entity-record',
  'rva': 9204564,
  'function': (9204544, 9204777),
  'signature': '488DBA48010000488BD9488BCF488D542448498BF0E872200000483B47100F8394000000488B57404C8D04C24D85C00F84830000004D8B004D85C0747B4885F67476',
  'instruction_lengths': (7, 3, 3, 5, 3, 5, 4, 6, 4, 4, 3, 6, 3, 3, 2, 3, 2)},
 {'name': 'component-presence-index',
  'rva': 9204630,
  'function': (9204544, 9204777),
  'signature': '440FB70E4D8B5018418BC148C1E806410FB6C9498B14C248D3EAF6C201751C',
  'instruction_lengths': (4, 4, 3, 4, 4, 4, 3, 3, 2)},
 {'name': 'component-storage-address',
  'rva': 9204689,
  'function': (9204544, 9204777),
  'signature': '430FB7944A840A0000418B4030430FB78C4A84000000480FAFC24803C8488BC34903482048890B48895308',
  'instruction_lengths': (9, 4, 9, 4, 3, 3, 4, 3, 4)},
 {'name': 'actor-predicted-state',
  'rva': 1964624,
  'function': None,
  'signature': '4C8BC10FB6CABA0100000048D3E241F680B101000001498B80F80B00007414498B88D80B0000490B80D00B000048F7D14823C1488BCA4823C8483BCA0F94C0C3',
  'instruction_lengths': (3, 3, 5, 3, 8, 7, 2, 7, 7, 3, 3, 3, 3, 3, 3, 1)})


ANCHORS += (
 {'name': 'ui-player-state-clientactor-callback-entry',
  'rva': 0x2afef0,
  'function': (0x2afef0, 0x2aff24),
  'signature': '40574883EC5041B828000000488D542420488BF9E8B7A8620041B828000000488D542420488BCFE8845D620084C00F84ED000000',
  'instruction_lengths': (2, 4, 6, 5, 3, 5, 6, 5, 3, 5, 2, 6)},
 {'name': 'ui-player-state-clientactor-bit-25-call',
  'rva': 0x2aff78,
  'function': (0x2aff24, 0x2b0011),
  'signature': '488B4C2428B225E8CCFAF2FF',
  'instruction_lengths': (5, 2, 5)},
 {'name': 'ui-player-state-clientactor-bit-21-call',
  'rva': 0x2affa4,
  'function': (0x2aff24, 0x2b0011),
  'signature': '488B4C2428B221E8A0FAF2FF',
  'instruction_lengths': (5, 2, 5)},
)

def _rva_for_image_pointer(image, pointer, label):
    if pointer < image.image_base or pointer >= image.image_base + image.image_size:
        raise VerificationError(f"StateFlag {label} pointer is outside the pinned image")
    return pointer - image.image_base


def verify_state_flag_metadata(image):
    descriptor = STATE_FLAG_DESCRIPTOR_RVA
    name_pointer_rva = descriptor + STATE_FLAG_QUALIFIED_NAME_POINTER_OFFSET
    name_rva = _rva_for_image_pointer(image, image.qword_at(name_pointer_rva), "type-name")
    qualified_name = image.c_string_at(name_rva)
    if qualified_name != "keen::actor::StateFlag":
        raise VerificationError("StateFlag qualified type name drift")

    count_word_rva = descriptor + STATE_FLAG_FIELD_COUNT_OFFSET
    count_word = image.qword_at(count_word_rva)
    field_count = count_word & 0xFFFFFFFF
    if field_count != STATE_FLAG_ENUM_COUNT:
        raise VerificationError("StateFlag enum field count drift")

    table_pointer_rva = descriptor + STATE_FLAG_ENUM_TABLE_POINTER_OFFSET
    table_pointer = image.qword_at(table_pointer_rva)
    expected_table_pointer = image.image_base + STATE_FLAG_ENUM_TABLE_RVA
    if table_pointer != expected_table_pointer:
        raise VerificationError("StateFlag enum table pointer drift")

    entries = {}
    for expected_name, (index, expected_value) in STATE_FLAG_EXPECTED_ENTRIES.items():
        if index >= field_count:
            raise VerificationError(f"StateFlag {expected_name} index exceeds reflected field count")
        record_rva = STATE_FLAG_ENUM_TABLE_RVA + index * STATE_FLAG_ENTRY_STRIDE
        entry_name_pointer = image.qword_at(record_rva)
        entry_name_rva = _rva_for_image_pointer(image, entry_name_pointer, f"{expected_name} name")
        entry_name = image.c_string_at(entry_name_rva)
        value_rva = record_rva + 0x10
        value = image.qword_at(value_rva)
        if entry_name != expected_name or value != expected_value:
            raise VerificationError(f"StateFlag {expected_name} enum name/value drift")
        entries[expected_name] = dict(index=index, record_rva=f"0x{record_rva:x}",
            name_rva=f"0x{entry_name_rva:x}", value_rva=f"0x{value_rva:x}", value=value)

    return dict(descriptor_rva=f"0x{descriptor:x}", qualified_name=qualified_name,
        qualified_name_pointer_rva=f"0x{name_pointer_rva:x}", qualified_name_rva=f"0x{name_rva:x}",
        field_count_word_rva=f"0x{count_word_rva:x}", field_count_word=f"0x{count_word:x}",
        field_count=field_count, enum_table_pointer_rva=f"0x{table_pointer_rva:x}",
        enum_table_rva=f"0x{STATE_FLAG_ENUM_TABLE_RVA:x}",
        entry_stride=f"0x{STATE_FLAG_ENTRY_STRIDE:x}", entries=entries)



BASE_ACTOR_DESCRIPTOR_RVA = 0x17BC850
GENERIC_ACTOR_DESCRIPTOR_RVA = 0x17BC900
CLIENT_ACTOR_DESCRIPTOR_RVA = 0x17BD910
BASE_ACTOR_NAME_RVA = 0x1C96F20
BASE_ACTOR_QUALIFIED_NAME_RVA = 0x1C97038
GENERIC_ACTOR_NAME_RVA = 0x1C99E64
GENERIC_ACTOR_QUALIFIED_NAME_RVA = 0x1C9A030
CLIENT_ACTOR_NAME_RVA = 0x1C9B758
CLIENT_ACTOR_QUALIFIED_NAME_RVA = 0x1C9B8D0
CLIENT_ACTOR_BASE_DESCRIPTOR_OFFSET = 0x38
UI_PLAYER_STATE_DESCRIPTOR_RVA = 0x1D501E8
UI_PLAYER_STATE_NAME_RVA = 0x1D52250
UI_PLAYER_STATE_CALLBACK_RVA = 0x2AFEF0


def _verify_component_descriptor(image, descriptor, short_name, qualified_name,
                                 short_name_rva, qualified_name_rva, expected_size):
    short_pointer = image.qword_at(descriptor)
    short_pointer_rva = _rva_for_image_pointer(image, short_pointer, short_name)
    short_length = image.qword_at(descriptor + 8)
    qualified_pointer = image.qword_at(descriptor + 0x20)
    qualified_pointer_rva = _rva_for_image_pointer(image, qualified_pointer, qualified_name)
    qualified_length = image.qword_at(descriptor + 0x28)
    packed_size = image.qword_at(descriptor + 0x40)
    size = packed_size & 0xFFFFFFFF
    if (short_pointer_rva != short_name_rva or
        image.c_string_at(short_pointer_rva) != short_name or
        short_length != len(short_name) or
        qualified_pointer_rva != qualified_name_rva or
        image.c_string_at(qualified_pointer_rva) != qualified_name or
        qualified_length != len(qualified_name) or
        size != expected_size):
        raise VerificationError(f"{qualified_name} reflection descriptor drift")
    return dict(descriptor_rva=f"0x{descriptor:x}", name=short_name,
        name_rva=f"0x{short_pointer_rva:x}", qualified_name=qualified_name,
        qualified_name_rva=f"0x{qualified_pointer_rva:x}",
        size=f"0x{size:x}", packed_size=f"0x{packed_size:x}")


def verify_client_actor_metadata(image):
    base_actor = _verify_component_descriptor(image, BASE_ACTOR_DESCRIPTOR_RVA,
        "BaseActor", "keen::ecs::BaseActor", BASE_ACTOR_NAME_RVA,
        BASE_ACTOR_QUALIFIED_NAME_RVA, 0xC20)
    generic_actor = _verify_component_descriptor(image, GENERIC_ACTOR_DESCRIPTOR_RVA,
        "Actor", "keen::ecs::Actor", GENERIC_ACTOR_NAME_RVA,
        GENERIC_ACTOR_QUALIFIED_NAME_RVA, 0xE10)
    client_actor = _verify_component_descriptor(image, CLIENT_ACTOR_DESCRIPTOR_RVA,
        "ClientActor", "keen::ecs::ClientActor", CLIENT_ACTOR_NAME_RVA,
        CLIENT_ACTOR_QUALIFIED_NAME_RVA, 0xC38)

    parent_pointer = image.qword_at(CLIENT_ACTOR_DESCRIPTOR_RVA +
        CLIENT_ACTOR_BASE_DESCRIPTOR_OFFSET)
    expected_parent = image.image_base + BASE_ACTOR_DESCRIPTOR_RVA
    if parent_pointer != expected_parent:
        raise VerificationError("ClientActor BaseActor descriptor link drift")
    base_size = int(base_actor["size"], 16)
    client_size = int(client_actor["size"], 16)
    if client_size <= base_size:
        raise VerificationError("ClientActor size does not extend BaseActor")

    first_type_pointer = image.qword_at(UI_PLAYER_STATE_DESCRIPTOR_RVA)
    first_type_pointer_rva = _rva_for_image_pointer(image, first_type_pointer,
        "update_ui_player_state first component type")
    first_type_length = image.qword_at(UI_PLAYER_STATE_DESCRIPTOR_RVA + 8)
    if (first_type_pointer != image.qword_at(CLIENT_ACTOR_DESCRIPTOR_RVA) or
        first_type_pointer_rva != CLIENT_ACTOR_NAME_RVA or
        image.c_string_at(first_type_pointer_rva) != "ClientActor" or
        first_type_length != len("ClientActor")):
        raise VerificationError("update_ui_player_state first component is not ClientActor")

    callback_name_pointer = image.qword_at(UI_PLAYER_STATE_DESCRIPTOR_RVA + 0x48)
    callback_name_rva = _rva_for_image_pointer(image, callback_name_pointer,
        "update_ui_player_state callback name")
    callback_name_length = image.qword_at(UI_PLAYER_STATE_DESCRIPTOR_RVA + 0x50)
    if (callback_name_rva != UI_PLAYER_STATE_NAME_RVA or
        image.c_string_at(callback_name_rva) != "update_ui_player_state" or
        callback_name_length != len("update_ui_player_state")):
        raise VerificationError("update_ui_player_state descriptor name drift")

    callback_pointer = image.qword_at(UI_PLAYER_STATE_DESCRIPTOR_RVA + 0x58)
    if callback_pointer != image.image_base + UI_PLAYER_STATE_CALLBACK_RVA:
        raise VerificationError("update_ui_player_state callback pointer drift")

    return dict(client_actor=client_actor,
        base_actor=base_actor,
        client_actor_inheritance=dict(parent_descriptor_rva=f"0x{BASE_ACTOR_DESCRIPTOR_RVA:x}",
            parent_size="0xC20", client_size="0xC38", extension_size="0x18",
            base_link_offset=f"0x{CLIENT_ACTOR_BASE_DESCRIPTOR_OFFSET:x}"),
        generic_actor=generic_actor,
        state_consumer=dict(descriptor_rva=f"0x{UI_PLAYER_STATE_DESCRIPTOR_RVA:x}",
            name="update_ui_player_state", first_component="keen::ecs::ClientActor",
            callback_rva=f"0x{UI_PLAYER_STATE_CALLBACK_RVA:x}",
            component_row_size="0x28", state_helper_rva="0x1dfa50",
            state_bits=(0x25, 0x21),
            callback_behavior="loads first component from [rsp+0x28] and passes it directly to the state helper"))


def verify_image(image):
    require_build(image, CLIENT_BUILD)
    if image.qword_at(0x1CAD2A0+8*8) != image.image_base+0x78CD00:
        raise VerificationError("current GameApplication reflection metadata drift")
    state_flag_enum = verify_state_flag_metadata(image)
    sites = [verify_site(image, spec) for spec in ANCHORS]
    client_actor_metadata = verify_client_actor_metadata(image)
    return dict(status="pinned client typed ClientActor life reader static layout matches",
        static_only=True, build=image.manifest(), anchors=sites,
        local_entity=verify_client_ownership(image), state_flag_enum=state_flag_enum,
        client_actor=client_actor_metadata,
        reader=dict(game_inline_rva="0x1f07cc0", client_slot="0x250", session_slot="0x52888",
            active_byte="0x20", scene_slot="0x180", world_slot="0x1c0",
            simulation_slot="0x3439c0", simulation_world_slot=8,
            client_actor_type_rva="0x17bd910", component_records="0x930", component_count="0x938",
            component_stride="0x100", reflection_slot="0x28", entity_map="0xcc4360",
            authoritative_state="0xbf8", prediction_flag="0x1b1", added_state="0xbd0",
            removed_state="0xbd8", dead_bit=state_flag_enum["entries"]["Dead"]["value"],
            spawning_bit=state_flag_enum["entries"]["Spawning"]["value"],
            max_components=1280, max_entity_capacity=1048576, max_probes=64,max_reads=4096),
        runtime_installed=False, live_gameplay_verified=False,
        limitations=["Repeated copies detect observed inconsistency, not undetectable ABA.",
            "Caller binds copied local identity to current host/capability approval.",
            "The reader selects the exact ClientActor descriptor, not generic Actor.",
            "Conservative life gate denies authoritative OR native effective Dead/Spawning."])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client",type=Path,required=True)
    parser.add_argument("--json",type=Path)
    args=parser.parse_args()
    result=verify_image(PEImage.read(args.client))
    if args.json:
        args.json.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(result["status"])

if __name__ == "__main__":
    main()
