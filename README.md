# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

# Multi Class

## Description

With this mod, you will be able to switch your character from one class to another, without losing any non-class specific information souch as quests, achievements, professions, mounts, and so on.

Features:
- Class Change: Change your class with the '.class change' command
- Shared Quest Log: By default, all of your classes share a quest long with one another.  This can be enabled and disabled on a class-by-class basis by the '.class sharequests' option.
- Shared Reputation: By default, all of your classes share reputations with one another.  This can be enabled and disabled on a class-by-class basis by the '.class sharereputation' option.
- See Classes: See all of your classes and properties with the '.class info'

## Installation
- You must have a core version updated to at least changeset [4321b8a](https://github.com/azerothcore/azerothcore-wotlk/commit/4321b8a4dee98fce5f7b66dae43afa44b5c22a12)
- IMPORTANT: You must set the "ClassMask" to 0 for all SkillLineAbility.dbc rows with ID 202

Install the same as any other standard installation.  See https://www.azerothcore.org/wiki/installation

## Credits
*  [Nathan Handley: Writing this module](https://github.com/NathanHandley)
