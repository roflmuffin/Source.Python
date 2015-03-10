# ../weapons/scripts.py

"""Provides weapon scripts functionalities."""

# =============================================================================
# >> IMPORTS
# =============================================================================
# Source.Python Imports
#   Core
from core import SOURCE_ENGINE
#   Paths
from paths import SP_DATA_PATH
#   Memory
from memory.manager import manager
#   Weapons
from weapons.constants import WeaponID


# =============================================================================
# >> FORWARD IMPORTS
# =============================================================================
# Source.Python Imports
#   Weapons
from _weapons._scripts import BaseWeaponInfo
from _weapons._scripts import WeaponInfo


# =============================================================================
# >> ALL DECLARATION
# =============================================================================
__all__ = ('BaseWeaponInfo',
           'WeaponInfo',
           'weapon_scripts'
           )


# =============================================================================
# >> GLOBAL VARIABLES
# =============================================================================
get_weapon_info = manager.create_pipe_from_file(SP_DATA_PATH.joinpath(
    'weapons', 'scripts', SOURCE_ENGINE + '.ini')).get_weapon_info

weapon_scripts = dict()
for weapon_id in WeaponID.values:
    weapon_info = get_weapon_info(weapon_id)
    if weapon_info is None:
        continue
    weapon_scripts[weapon_info.class_name] = weapon_info