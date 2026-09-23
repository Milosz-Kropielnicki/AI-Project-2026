# MechaByte — Reorganized Design Document

> Reorganization only. All original concepts, names, and rules are retained. Sections have been regrouped and renumbered for readability.

---

## 1. Core Concept

The Player is a **Mercenary Combat Engineer**. They travel a hyper-futuristic world, taking on jobs to fight rogue AI and Criminal Organizations.

---

## 2. Core Gameplay Loop

**Explore → Find Rogue AI/Criminal in possession of mech/robot → Battle → Reprogram/Scrap/Upgrade → Explore → Battle Stronger Opponents → Acquire New Robots**

### Battle Options

- **Hacking Tools** — Reprogram the opposing robot/mech. This is the **capture mechanic**.
- **Scrap** — Destroy the opposing robot/mech. This is the **faint mechanic**.

The game follows a turn-based gameplay mix of **Pokémon** and **Slay the Spire**.

### Acquiring New Mechs/Robots

The Player can:

- Purchase them from dedicated manufacturing companies
- Reprogram them
- Receive new units as rewards

---

## 3. Attributes

| Attribute | Range / Type | Description |
|---|---:|---|
| **Integrity** | 0–200 | The health pool. |
| **Power** | 0.50x–2.00x | Weapon/Attack damage multiplier. Baseline is 1x, meaning all attacks/weapons deal their base damage. |
| **Armor** | 0–150 | Additional pool placed on top of Integrity that absorbs damage. |
| **Mobility** | 0–100% | Chance to evade attacks. |
| **Energy** | 1–5 actions | How many actions can be taken in a single turn. |
| **Heat** | 0–200 | What actions can be performed in a turn without negative effects. |
| **Accuracy** | 0–100% | Chance for attack to hit. |
| **Stability** | 0–100% | Chance to avoid system scrambling. |

---

## 4. Combat Formulas

### 4.1 Damage

**Raw Damage = Weapon Base Damage × Power**

### 4.2 Armor

Armor is the finite, secondary HP pool that absorbs raw damage.

- **Damage to Armor** = min(Raw Damage, Current Armor)
- **Damage to Integrity** = max(0, Raw Damage − Current Armor)
- **Current Armor** = Current Armor − Raw Damage

### 4.3 Armor-Breaking / Armor Penetration

Armor Penetration is the passthrough of raw Damage vs. Armor, using a custom Penetration value in a weapon.

Examples:

| Weapon | Damage | Armor Penetration |
|---|---:|---:|
| Machine Gun | 100 | 10% |
| Railgun | 120 | 60% |
| Anti-Armor Missile | 110 | 80% |

- **Damage to Integrity** = Weapon Base Damage × Armor Penetration
- **Damage to Armor** = Weapon Base Damage − (Weapon Base Damage × Armor Penetration)

### 4.4 Hit Chance

**Accuracy vs. Mobility**

**Hit Chance = Base Weapon Accuracy × Attacker Accuracy Modifier × Target Evasion Modifier**

Where:

- **Attacker Accuracy Modifier** = Accuracy / 100
- **Target Evasion Modifier** = 1 − (Target Mobility / 200)

Clamp:

- **Minimum Hit Chance** = 5%
- **Maximum Hit Chance** = 95%

### 4.5 Energy

Energy is treated as **Action Points**. It works as action economy. It does **not** boost stats.

**Current Energy = Max Energy at the start of each round.**

### 4.6 Stability and Scrambling

Stability governs stability to **system scrambling**, not general resistance.

A **scramble** is an electronic disruption. Depending on strength, it can cause:

- Lose an Energy
- Reduce Accuracy
- Disable a weapon
- Lose turn

**Resistance = Stability / (Stability + Scramble Strength)**

Example:

- Scramble Strength = 70
- Stability = 75
- 75 / (75 + 70) = ~51.7%

### 4.7 Full Attack Resolution

**Step 1 — Determine Hit Chance**

**Hit Chance = Weapon Accuracy × Attacker Accuracy Modifier × Target Evasion Modifier**

Where:

- **Attacker Accuracy Modifier** = Accuracy / 100
- **Target Evasion Modifier** = 1 − Mobility / 200

Clamp: **5%–95%**

**Step 2 — Roll to Hit**

- If successful: Continue.
- If unsuccessful: Miss.

**Step 3 — Calculate Raw Damage**

**Raw Damage = Weapon Base Damage × Power**

**Step 4 — Apply Weapon Modifiers**

**Raw Damage × Damage Modifier**

**Step 5 — Split Damage Between Armor and Integrity**

If weapon has Armor Penetration:

- **Armor Damage** = Raw Damage × (1 − Penetration)
- **Integrity Damage** = Raw Damage × Penetration

Then apply the remaining Armor damage to the Armor pool.

Any Armor damage exceeding current Armor spills into Integrity.

---

## 5. Classes and Roles

### 5.1 Role Descriptions

| Class | Role | Description |
|---|---|---|
| **Heavy Assault** | **Breacher** | Specialized Unit focused on close-quarter combat engagements against armored targets. Focused more on close-quarter firepower over anything else. |
| **Heavy Assault** | **Dreadnought** | Heavy-plated, slow-moving hulls designed to absorb massive firepower and offer heavy damage. An excellent all-rounder. |
| **Heavy Assault** | **Juggernaut** | Shock-infantry designed for aggressive combat. A lot of firepower, reasonably mobile, okay at taking a beating. |
| **Heavy Assault** | **Ironclad** | Heavily armoured defensive anchors designed to mitigate damage and focus enemy fire. Exceptional defence, but suffers at mobility and offensive capabilities. |
| **Artillery** | **Bombard** | Long-range siege artillery designed for firing salvos of high-yield mortars. |
| **Artillery** | **Ordnance** | Deployed heavy gun platforms designed for armor cracking and shield stripping. |
| **Artillery** | **Arbalest** | High-precision kinetic snipers focused on weak-point target hunting. |
| **Artillery** | **Battery** | Sustained-fire, high-capacity low-kinetic rotary cannons designed at saturating and overwhelming enemy positions and weaker targets. |
| **Recon** | **Infiltrator** | Stealth-coated, low-profile, capable of bypassing sensor arrays to strike vulnerable positions and targets. |
| **Recon** | **Skirmisher** | High-agility, high-speed hulls designed for hit-and-run flanking maneuvers. |
| **Recon** | **Scout** | Rapid-response scouts equipped with light weaponry and fast-locking target systems. |
| **Recon** | **Prowler** | Ambush specialists using active camouflage and high-burst short-range weaponry. |
| **Electronic Warfare** | **Catcher** | Fleet-commanders that boost ally targeting parameters and coordinate artillery strikes. |
| **Electronic Warfare** | **Disruptor** | Electronic warfare platform designed for radar jamming, system disabling, and scrambling telemetry. |
| **Electronic Warfare** | **Sapper** | Field support capable of deploying directional shields and hazard material to inhibit enemy weaponry and movement. |
| **Electronic Warfare** | **Aegis** | Shield-projection unit that deploys energy barriers to protect allies. |

### 5.2 Class Baseline Fallback Attributes

| Class | Integrity | Power | Armor | Mobility | Energy | Accuracy | Stability |
|---|---:|---:|---:|---:|---:|---:|---:|
| **Heavy Assault** | 150 | 1.15x | 80 | 35% | 2 | 75% | 75% |
| **Artillery** | 100 | 1.20x | 35 | 20% | 2 | 90% | 65% |
| **Recon** | 75 | 0.90x | 15 | 75% | 3 | 80% | 60% |
| **Electronic Warfare** | 90 | 0.75x | 25 | 45% | 3 | 85% | 90% |

### 5.3 Role Baseline Attributes

| Class | Role | Integrity | Power | Armor | Mobility | Energy | Accuracy | Stability |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| **Heavy Assault** | Breacher | 145 | 1.30x | 70 | 40% | 2 | 70% | 70% |
| **Heavy Assault** | Dreadnought | 180 | 1.15x | 100 | 25% | 2 | 75% | 85% |
| **Heavy Assault** | Juggernaut | 155 | 1.25x | 70 | 50% | 2 | 75% | 75% |
| **Heavy Assault** | Ironclad | 175 | 0.90x | 125 | 20% | 2 | 65% | 90% |
| **Artillery** | Bombard | 105 | 1.30x | 40 | 15% | 2 | 80% | 65% |
| **Artillery** | Ordnance | 110 | 1.25x | 50 | 20% | 2 | 90% | 70% |
| **Artillery** | Arbalest | 80 | 1.45x | 20 | 25% | 2 | 98% | 60% |
| **Artillery** | Battery | 115 | 1.00x | 40 | 15% | 3 | 85% | 75% |
| **Recon** | Infiltrator | 65 | 1.00x | 10 | 90% | 3 | 85% | 50% |
| **Recon** | Skirmisher | 75 | 1.00x | 15 | 100% | 4 | 80% | 60% |
| **Recon** | Scout | 70 | 0.80x | 10 | 85% | 4 | 95% | 65% |
| **Recon** | Prowler | 65 | 1.20x | 10 | 85% | 3 | 85% | 50% |
| **Electronic Warfare** | Catcher | 85 | 0.65x | 20 | 45% | 4 | 95% | 95% |
| **Electronic Warfare** | Disruptor | 90 | 0.75x | 25 | 40% | 4 | 90% | 100% |
| **Electronic Warfare** | Sapper | 100 | 0.70x | 40 | 35% | 3 | 85% | 90% |
| **Electronic Warfare** | Aegis | 110 | 0.60x | 50 | 25% | 3 | 80% | 95% |

---

## 6. Models

Models are the individual models that can be found within a given Class Role.

The naming scheme uses **Class Initials-Role Initials-Model Number "Name"**.

Examples:

- **R-P-07 "Hound"** — A small quadruped reconnaissance robot.
- **HA-D-32 "Bulwark"** — A massive humanoid assault mech.

---

## 7. Firmware Revision System

### 7.1 Overview

The chassis and refit determine what a machine is capable of physically doing, while **Firmware** determines how intelligently, efficiently, and flexibly it operates.

The structure system has four layers:

**Firmware Revision → Instruction Sockets → Algorithmic Chips → Combat Instructions**

### 7.2 Firmware Revision Levels

Every robot has a **Firmware Revision Level**.

For example:

- **Firmware Revision 1.0** could represent a newly manufactured or freshly restored machine.

As it gains combat experience:

- Firmware Revision 1.1
- Firmware Revision 1.2
- Firmware Revision 1.3
- …and eventually **Firmware Revision 2.0**, etc.

### 7.3 What Causes Firmware Revisions?

The robot earns **Revision Data** through combat.

Essentially:

**Battle Experience → Revision Data → Firmware Revision**

Revision Data could come from:

- Destroying enemy machines
- Participating in battles
- Completing missions
- Fighting higher-level enemies
- Successfully executing certain behaviors
- Discovering new technology
- Defeating bosses

### 7.4 Firmware Revision Milestones

Each Revision level should have a meaningful milestone.

| Revision | Unlock |
|---:|---|
| 1.0 | Basic firmware |
| 1.1 | Instruction Socket |
| 1.2 | Minor firmware optimization |
| 1.3 | Instruction Socket |
| 1.4 | Algorithmic Capacity increase |
| 1.5 | Instruction Socket |
| 2.0 | Major Firmware Revision |
| 2.1 | Instruction Socket |
| 2.2 | Firmware Optimization |
| 2.3 | Instruction Socket |
| 2.4 | Algorithmic Capacity |
| 2.5 | Instruction Socket |
| 3.0 | Major Firmware Revision |

This means every level doesn’t necessarily give a new ability. Instead, the player is constantly progressing toward meaningful milestones.

### 7.5 Major vs. Minor Revisions

**Minor Revisions**

Examples: 1.1 → 1.2 → 1.3

Small improvements.

Examples:

- +2 Accuracy
- +2 Stability
- +5 maximum Heat
- +5 Armor
- −1% weapon Heat generation

**Major Revisions**

Examples: 1.0 → 2.0 → 3.0

Significant software improvements.

These unlock:

- New Instruction Socket
- New Algorithmic Chip categories
- Passive abilities
- New combat behaviors
- Additional action types
- New firmware architecture

### 7.6 Instruction Sockets

These are essentially your card slots.

A robot starts with perhaps:

- **2 Instruction Sockets**

and progressively unlocks more.

Example progression:

| Firmware | Sockets |
|---:|---:|
| 1.0 | 2 |
| 1.5 | 3 |
| 2.0 | 4 |
| 2.5 | 5 |
| 3.0 | 6 |

The cap is around **6–8 sockets**.

### 7.7 Algorithmic Chips

These are Slay the Spire-inspired cards.

A Chip is installed into an Instruction Socket. Once installed, it provides an ability, behavior, or modifier.

Examples:

- **Target Prioritization Algorithm** — When selecting an attack, prioritize the enemy with the lowest Integrity.
- **Emergency Evasion Protocol** — When Integrity falls below 25%, gain +30 Mobility for one turn.
- **Armor Breach Routine** — The first attack against an enemy with Armor deals +25% Armor damage.
- **Suppression Algorithm** — After successfully damaging an enemy, reduce its Accuracy by 5% for one turn.

These aren’t new weapons. They’re software instructions telling the robot how to use its hardware.

### 7.8 Chip Types / Categories

#### Offensive Algorithms

Modify attacks.

Examples:

- **Overcharge Routine** — Next Energy weapon deals +30% damage. Generates additional Heat.
- **Precision Strike** — Next attack gains +20% Accuracy.
- **Armor Analysis** — Attacks against targets with >50 Armor gain +20% Armor Penetration.

#### Defensive Algorithms

Modify survival.

Examples:

- **Reactive Plating** — When Armor is destroyed, gain temporary Shielding.
- **Emergency Power Routing** — When Integrity falls below 25%, gain +1 Energy next turn.
- **Damage Mitigation** — Reduce the next incoming attack by 30%.

#### Mobility Algorithms

Modify movement/evasion.

Examples:

- **Evasive Maneuver** — After attacking, gain +20 Mobility until your next turn.
- **Flanking Protocol** — Attacking a target that has already been attacked this turn grants +15% Accuracy.

#### Electronic Warfare Algorithms

Particularly important for the EW class.

Examples:

- **Counter-Intrusion** — +25 Stability when resisting Scramble.
- **Signal Cascade** — Successful Scramble has a 20% chance to spread to another enemy.
- **Targeting Spoof** — First attack against this robot each round has −20% Accuracy.

#### Behavioral Algorithms

Some Chips shouldn’t be manually activated at all. Instead, they establish IF/THEN behaviors.

Examples:

- **Emergency Repair Protocol** — IF Integrity < 30%, THEN automatically consume 2 Energy to initiate Repair.
- **Anti-Armor Priority** — IF target Armor > 50, THEN prioritize Armor-Penetrating weapons.
- **Threat Response** — IF targeted by Artillery, THEN activate Evasive Maneuver.

This makes Firmware feel genuinely like programming an autonomous machine.

#### Passive Chips

Some Chips simply modify the robot.

Examples:

- **Thermal Optimization** — −10% Heat generated by Energy weapons.
- **Hardened Kernel** — +10 Stability.
- **Predictive Targeting** — +5 Accuracy.
- **Efficient Power Distribution** — The first action each turn costs 0 Energy.

These are effectively the equivalent of passive relics.

#### Triggered Chips

Something happens → Chip activates.

Examples:

- **Retaliation Protocol** — When damaged by a melee attack, immediately gain +20 Power for your next attack.
- **Last Stand** — When reduced below 10% Integrity, gain +2 Energy.
- **Kill Confirmation** — Destroying an enemy restores 1 Energy.
- **System Recovery** — Successfully resisting a Scramble restores 10 Integrity.

This is where the Slay the Spire inspiration becomes particularly valuable. You can create synergies between Chips.

### 7.9 Chip Rarity

- **Standard** — Common, reliable algorithms.
- **Advanced** — More specialized.
- **Experimental** — Stronger effects with drawbacks.
- **Prototype** — Extremely powerful, highly specific effects.
- **Black Box** — Rare algorithms with unusual mechanics.

Higher rarity doesn’t automatically mean better. Instead:

- **Common** = flexible
- **Rare** = specialized
- **Experimental** = powerful but conditional

### 7.10 Chip Capacity

One problem with card systems is that players can accumulate dozens of cards. You don’t want:

> “Here’s my 700-chip robot.”

So distinguish:

- **Chip Collection** — Everything the player owns.
- **Installed Firmware** — Only the Chips currently installed.

For example:

- 20 Chips owned
- but 5 Instruction Sockets

Only five can actually be active.

This creates a loadout-building layer.

### 7.11 Chip Removal

Chips are removable. Otherwise players will be terrified of experimenting.

Chips can be freely swapped outside combat.

The strategic choice should be:

> “Which five do I want?”

not:

> “I accidentally installed the wrong one and now I’m screwed.”

### 7.12 Firmware Decks / Profiles

Each robot has a **Firmware Profile**.

For example:

**Arbalest — Precision Profile**

Installed:

- Predictive Targeting
- Weak-Point Analysis
- Armor Analysis
- Ballistic Calibration
- Critical Strike Routine

The player could save multiple configurations:

- Precision Profile
- Anti-Armor Profile
- Boss Profile

That would be particularly useful if refitting is a major part of the game.

### 7.13 Firmware Archetypes

Certain Chips interact with each other.

#### “Overclock” Archetype

Chips:

- Overclock Routine
- Emergency Cooling
- Power Surge
- Reactor Bypass
- Thermal Venting

The robot becomes:

**High Energy → Huge Damage → Massive Heat → Cool Down → Repeat**

#### “Evasion” Archetype

Chips:

- Evasive Maneuver
- Predictive Movement
- Reactive Thrusters
- Targeting Spoof

The robot becomes:

**Attack → Move → Become difficult to hit → Attack again**

#### “Scramble” Archetype

Chips:

- Signal Cascade
- EMP Amplification
- System Blackout
- Neural Disruption

The Disruptor becomes a machine that chains electronic attacks across the enemy team.

### 7.14 Firmware and Existing Attributes

This is extremely important.

Firmware should not simply dump +50 to every stat. Instead, firmware should primarily provide **behavioral modifications**.

The seven primary attributes remain the province of:

**Class + Role + Chassis + Refits**

Firmware modifies how those attributes are used.

For example:

**Arbalest — Accuracy 98**

Firmware:

- **Precision Targeting Algorithm** — Critical hits against targets with <50% Integrity deal +25% damage.

The robot didn’t suddenly become more accurate. It became better at exploiting the accuracy it already possesses. That’s a much more satisfying distinction.

### 7.15 Firmware Can Still Provide Minor Stat Improvements

There should be some numerical progression. Otherwise the player may feel like leveling isn’t making their robot stronger.

Firmware provides small **Optimization Points** at certain milestones.

For example, at **Revision 1.2**, choose:

- +5 Integrity
- +3 Armor
- +3 Accuracy
- +3 Stability
- +5 Mobility
- +5 Heat Capacity

This lets the player gradually personalize their robot. But the numbers should be relatively small.

### 7.16 Firmware Specializations

At a major revision, the player can choose a **Firmware Branch**.

For example, an Arbalest reaching Firmware 3.0 might choose:

- **Hunter Kernel** — Focuses on critical hits, weak points, target marking, precision.
- **Siege Kernel** — Focuses on armor penetration, heavy weapons, damage, heat management.
- **Ghost Kernel** — Focuses on stealth, mobility, evasion, ambushes.

Two Arbalests can reach the same Firmware Revision but have fundamentally different software.

### 7.17 Firmware Quirks / Traits

Firmware Traits are analogous to a Pokémon Ability.

Examples:

- **Aggressive Combat Kernel** — Gains +10% Power while below 50% Integrity.
- **Defensive Kernel** — First attack received each battle deals 15% less damage.
- **Efficient Kernel** — First action each turn costs 0 Energy.
- **Adaptive Kernel** — Gains a small bonus against the damage type that last damaged it.

These could be tied to the individual robot.

### 7.18 Elite / Boss Firmware

Boss robots break normal rules.

Examples:

**Factory Overseer**

- **Recursive Targeting Algorithm** — Every time an attack misses, Accuracy increases by 10%.

**Ancient War Machine**

- **Dead-Man Protocol** — Upon reaching 0 Integrity, immediately executes one final attack.

These Algorithms could then become extremely valuable post-battle.

### 7.19 Firmware Corruption

Certain enemies might inflict **Firmware Corruption**.

Temporary effects could include:

- Chips disabled
- Instructions reversed
- Increased Energy costs
- Random targeting
- Algorithm conflicts

A Disruptor could therefore attack not only the player’s robot’s Stability but its installed firmware. That makes Electronic Warfare particularly distinctive.

### 7.20 Firmware Capacity

Second restriction besides sockets.

- **Instruction Sockets** — Determine how many Chips you can install.
- **Processing Capacity** — Determines how powerful those Chips can be collectively.

For example:

**Firmware 2.0**

- 4 Sockets
- 10 Processing Capacity

A Chip might cost:

- 1–5 Processing Capacity

So you might install:

- Five cheap Chips
- or two extremely powerful Chips

This gives another layer of buildcraft without simply increasing the number of sockets indefinitely.

### 7.21 Hierarchy

- **Firmware** — The robot’s overall operating software.
- **Instruction Socket** — A processing slot within the firmware architecture.
- **Algorithmic Chip** — A modular software package installed into a socket.
- **Algorithm** — The actual ability/behavior.

So the UI says:

```text
FIRMWARE REVISION 3.2
Processing Capacity: 14/15
Instruction Sockets: 6/6

[ALGORITHM CHIP] Predictive Targeting
[ALGORITHM CHIP] Armor Analysis
[ALGORITHM CHIP] Emergency Evasion
```

### 7.22 Other Leveling Mechanics

1. **New Instruction Sockets** — The primary progression.
2. **Processing Capacity** — Allows stronger Algorithms.
3. **Firmware Branches** — Choose a specialization at major revisions.
4. **Optimization Points** — Small permanent stat improvements.
5. **Firmware Traits** — Individual robot passive abilities.
6. **New Instruction Types** — Higher-level firmware might unlock:
   - Reactive Algorithms
   - Conditional Algorithms
   - Chain Algorithms
   - Autonomous Algorithms
7. **New Command Functions** — A robot might initially only have:
   - Attack
   - Defend

   Later firmware unlocks:
   - Target Priority
   - Guard Ally
   - Focus Fire
   - Overwatch
   - Intercept
   - Retreat

8. **Chip Synergies** — Certain Algorithms interact. For example, **Overclock + Thermal Venting** creates a completely different playstyle from either Chip alone.
9. **Firmware Mastery** — A robot that repeatedly uses a particular weapon could unlock firmware specifically adapted to it. For example:
   - Railgun Mastery
   - Missile Guidance Mastery
   - Melee Combat Kernel

   This gives individual machines a sense of history.

10. **Legacy Firmware** — Perhaps some old robots contain obsolete but powerful algorithms that modern machines cannot normally use. That could create an ancient technology vs. modern technology dimension to the setting.

---

## 8. Chassis Refit (Upgrades)

### 8.1 Overview

The Chassis Refit is the layer that turns the class/role into an actual individual machine.

- The **class** says what the machine is built to do.
- The **role** says how it does that job.
- The **refit** says how this particular machine is configured.

That distinction is useful because modular mech systems work particularly well when different components contribute concrete gameplay statistics rather than being purely cosmetic.

The five refit sections are mechanically distinct:

| Section | Mechanical Focus |
|---|---|
| **Weapon System** | What it does to the enemy |
| **Head** | How it perceives and targets |
| **Body** | How it survives and powers itself |
| **Arms** | How it handles equipment |
| **Legs** | How it moves and avoids attacks |

### 8.2 Weapon System

**Weapon Platform:**

- Machine Gun
- Shotguns
- Railguns
- Missiles
- Flamethrowers
- Lasers
- Grenade Launchers
- Rocket Pods
- Blades (Physical/Energy)

**Munition Type:**

- Ballistic
- Energy
- Thermal
- Electromagnetic
- Explosive
- Chemical

### 8.3 Head

Focused on primary electronic system components:

- Sensors
- Targeting Systems
- Radars
- Electronic Warfare Modules

### 8.4 Body

Focused on armor and energy:

- Armor
- Reactor
- Cooling System

### 8.5 Arms

Focused on equipment (Equipment slots):

- Weapons
- Shields
- Manipulators

### 8.6 Legs

Focused on movement:

- Treads
- Legs
- Hover Systems
- Jump Jets

### 8.7 Refit Module Details

#### Weapon System

Every weapon has a basic profile:

**Weapon Platform:**

- Base Damage — The base, starting damage of the weapon
- Energy Cost — The number of action points needed to use the weapon
- Accuracy — Hit chance % modifier
- Range — Valid target distance
- Armor Penetration — Armor bypass % modifier
- Targeting — Single / Area / Cone / Line
- Heat — The amount of heat this weapon generates and is subsequently applied to the robot

**Munition Type:**

- **Ballistic** — Moderate armor penetration, low status effects, high reliability
- **Energy** — High accuracy, low armor penetration
- **Thermal** — Low armor penetration, inflicts thermal effects (Melt, Overheat, etc.) causing increased Energy costs, disabling high-energy weapons, etc. for opponents
- **Electromagnetic** — Low damage, no armor penetration, high scramble modifier
- **Explosive** — High damage, moderate armor penetration, splash damage
- **Chemical** — Low damage, low armor penetration, inflicts chemical effects (Corrosion) reducing enemy max armor or reducing stability over time for opponents

#### Head

Information and Targeting Subsystem.

- **Sensors** — Determines what can be perceived, interacting with Detection, Stealth, Weak Points, etc.
- **Targeting System** — Determines attack effectiveness. E.g. Increased accuracy, target switching, etc.
- **Radars** — Determines detection range. E.g. Allows attacks and target locking onto further enemies (increased accuracy).
- **Electronic Warfare Modules** — Modifies scramble strength, range, duration, and detection.

#### Body

Survivability and Power Management.

- **Armor** — Changes max armor and mobility. E.g. +50 Armor but −15 Mobility, or −15 Armor but +20 Mobility.
- **Reactor** — Energy modifier. Offers increased or reduced base energy, with effect on stability and power. E.g. +2 Energy and +0.10 Power but −10 Stability.
- **Cooling System** — Controls how much heat the robot can handle, and how much of it dissipates per turn. E.g. Heat Capacity is 100 and Cooling is 40/turn, and Laser +20 Heat per use.

#### Arms

Acts as an equipment interface.

- **Weapons** — The exact Weapon Platforms installed onto the robot/mech. Includes weapon specific modifiers. E.g. +5 accuracy with ballistic weapons, +10% damage with melee.
- **Shields** — Control various defensive mechanism controls. E.g. Minor Armor Restoration, Boosting Armor Effectiveness, Temporary Shield blocking directional damage.
- **Manipulators** — Utility modules. Allow for: Salvaging scrapped robots/mechs for blueprints/parts, repairing (Integrity) during or post battle, hacking (Pokémon capture mechanic).

#### Legs

Mobility subsystem.

- **Treads** — High stability, high armor capacity, improved heavy-weapon handling, but low mobility.
- **Legs** — Moderate stability.
- **Hover Systems** — High mobility, low stability, lower armor capacity.
- **Jump Jets** — Additional action added to above options. One-turn dramatic mobility increase.

---

## 9. Refit Rating

To avoid nonsensical combinations that erase class and role identity, all Chassis Refit modules should have a **compatibility rating between 1 and 5**, 1 being lowest and 5 highest.

- A rating of **5** gives the full module stats/abilities.
- For each rating below, the module stats/abilities are reduced.
- At **1 star**, they should be so abysmal, it’s not even worth it, and it’s closer to a detriment.

---

## 10. Ultimate Design Philosophy

**Class → Role → Chassis → Refit → Weapon → Munition**