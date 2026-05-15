/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/games/tf2/sdk/entities/weapon.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef WEAPON_HPP
#define WEAPON_HPP

#include "entity.hpp"

#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/netvars.hpp"

#include "core/print.hpp"

#include <algorithm>
#include <cmath>

#define TICK_INTERVAL 0.015

//https://github.com/Fedoraware/Fedoraware/blob/888d9338dbf08f6c55816c86d5887dd58cc28d98/Fedoraware/Fedoraware-TF2/src/SDK/Includes/Enums.h#L1081
enum {
  Scout_m_Scattergun = 13,
  Scout_m_ScattergunR = 200,
  Scout_m_ForceANature = 45,
  Scout_m_TheShortstop = 220,
  Scout_m_TheSodaPopper = 448,
  Scout_m_FestiveScattergun = 669,
  Scout_m_BabyFacesBlaster = 772,
  Scout_m_SilverBotkillerScattergunMkI = 799,
  Scout_m_GoldBotkillerScattergunMkI = 808,
  Scout_m_RustBotkillerScattergunMkI = 888,
  Scout_m_BloodBotkillerScattergunMkI = 897,
  Scout_m_CarbonadoBotkillerScattergunMkI = 906,
  Scout_m_DiamondBotkillerScattergunMkI = 915,
  Scout_m_SilverBotkillerScattergunMkII = 964,
  Scout_m_GoldBotkillerScattergunMkII = 973,
  Scout_m_FestiveForceANature = 1078,
  Scout_m_TheBackScatter = 1103,
  Scout_m_NightTerror = 15002,
  Scout_m_TartanTorpedo = 15015,
  Scout_m_CountryCrusher = 15021,
  Scout_m_BackcountryBlaster = 15029,
  Scout_m_SpruceDeuce = 15036,
  Scout_m_CurrentEvent = 15053,
  Scout_m_MacabreWeb = 15065,
  Scout_m_Nutcracker = 15069,
  Scout_m_BlueMew = 15106,
  Scout_m_FlowerPower = 15107,
  Scout_m_ShottoHell = 15108,
  Scout_m_CoffinNail = 15131,
  Scout_m_KillerBee = 15151,
  Scout_m_Corsair = 15157,
  Scout_s_ScoutsPistol = 23,
  Scout_s_PistolR = 209,
  Scout_s_BonkAtomicPunch = 46,
  Scout_s_VintageLugermorph = 160,
  Scout_s_CritaCola = 163,
  Scout_s_MadMilk = 222,
  Scout_s_Lugermorph = 294,
  Scout_s_TheWinger = 449,
  Scout_s_PrettyBoysPocketPistol = 773,
  Scout_s_TheFlyingGuillotine = 812,
  Scout_s_TheFlyingGuillotineG = 833,
  Scout_s_MutatedMilk = 1121,
  Scout_s_FestiveBonk = 1145,
  Scout_s_RedRockRoscoe = 15013,
  Scout_s_HomemadeHeater = 15018,
  Scout_s_HickoryHolepuncher = 15035,
  Scout_s_LocalHero = 15041,
  Scout_s_BlackDahlia = 15046,
  Scout_s_SandstoneSpecial = 15056,
  Scout_s_MacabreWeb = 15060,
  Scout_s_Nutcracker = 15061,
  Scout_s_BlueMew = 15100,
  Scout_s_BrainCandy = 15101,
  Scout_s_ShottoHell = 15102,
  Scout_s_DressedToKill = 15126,
  Scout_s_Blitzkrieg = 15148,
  Scout_s_TheCAPPER = 30666,
  Scout_t_Bat = 0,
  Scout_t_BatR = 190,
  Scout_t_TheSandman = 44,
  Scout_t_TheHolyMackerel = 221,
  Scout_t_TheCandyCane = 317,
  Scout_t_TheBostonBasher = 325,
  Scout_t_SunonaStick = 349,
  Scout_t_TheFanOWar = 355,
  Scout_t_TheAtomizer = 450,
  Scout_t_ThreeRuneBlade = 452,
  Scout_t_TheConscientiousObjector = 474,
  Scout_t_UnarmedCombat = 572,
  Scout_t_TheWrapAssassin = 648,
  Scout_t_FestiveBat = 660,
  Scout_t_TheFreedomStaff = 880,
  Scout_t_TheBatOuttaHell = 939,
  Scout_t_TheMemoryMaker = 954,
  Scout_t_FestiveHolyMackerel = 999,
  Scout_t_TheHamShank = 1013,
  Scout_t_TheNecroSmasher = 1123,
  Scout_t_TheCrossingGuard = 1127,
  Scout_t_Batsaber = 30667,
  Scout_t_PrinnyMachete = 30758,
  Soldier_m_RocketLauncher = 18,
  Soldier_m_RocketLauncherR = 205,
  Soldier_m_TheDirectHit = 127,
  Soldier_m_TheBlackBox = 228,
  Soldier_m_RocketJumper = 237,
  Soldier_m_TheLibertyLauncher = 414,
  Soldier_m_TheCowMangler5000 = 441,
  Soldier_m_TheOriginal = 513,
  Soldier_m_FestiveRocketLauncher = 658,
  Soldier_m_TheBeggarsBazooka = 730,
  Soldier_m_SilverBotkillerRocketLauncherMkI = 800,
  Soldier_m_GoldBotkillerRocketLauncherMkI = 809,
  Soldier_m_RustBotkillerRocketLauncherMkI = 889,
  Soldier_m_BloodBotkillerRocketLauncherMkI = 898,
  Soldier_m_CarbonadoBotkillerRocketLauncherMkI = 907,
  Soldier_m_DiamondBotkillerRocketLauncherMkI = 916,
  Soldier_m_SilverBotkillerRocketLauncherMkII = 965,
  Soldier_m_GoldBotkillerRocketLauncherMkII = 974,
  Soldier_m_FestiveBlackBox = 1085,
  Soldier_m_TheAirStrike = 1104,
  Soldier_m_WoodlandWarrior = 15006,
  Soldier_m_SandCannon = 15014,
  Soldier_m_AmericanPastoral = 15028,
  Soldier_m_SmalltownBringdown = 15043,
  Soldier_m_ShellShocker = 15052,
  Soldier_m_AquaMarine = 15057,
  Soldier_m_Autumn = 15081,
  Soldier_m_BlueMew = 15104,
  Soldier_m_BrainCandy = 15105,
  Soldier_m_CoffinNail = 15129,
  Soldier_m_HighRollers = 15130,
  Soldier_m_Warhawk = 15150,
  Soldier_s_SoldiersShotgun = 10,
  Soldier_s_ShotgunR = 199,
  Soldier_s_TheBuffBanner = 129,
  Soldier_s_Gunboats = 133,
  Soldier_s_TheBattalionsBackup = 226,
  Soldier_s_TheConcheror = 354,
  Soldier_s_TheReserveShooter = 415,
  Soldier_s_TheRighteousBison = 442,
  Soldier_s_TheMantreads = 444,
  Soldier_s_FestiveBuffBanner = 1001,
  Soldier_s_TheBASEJumper = 1101,
  Soldier_s_FestiveShotgun = 1141,
  Soldier_s_PanicAttack = 1153,
  Soldier_s_BackwoodsBoomstick = 15003,
  Soldier_s_RusticRuiner = 15016,
  Soldier_s_CivicDuty = 15044,
  Soldier_s_LightningRod = 15047,
  Soldier_s_Autumn = 15085,
  Soldier_s_FlowerPower = 15109,
  Soldier_s_CoffinNail = 15132,
  Soldier_s_DressedtoKill = 15133,
  Soldier_s_RedBear = 15152,
  Soldier_t_Shovel = 6,
  Soldier_t_ShovelR = 196,
  Soldier_t_TheEqualizer = 128,
  Soldier_t_ThePainTrain = 154,
  Soldier_t_TheHalfZatoichi = 357,
  Soldier_t_TheMarketGardener = 416,
  Soldier_t_TheDisciplinaryAction = 447,
  Soldier_t_TheConscientiousObjector = 474,
  Soldier_t_TheEscapePlan = 775,
  Soldier_t_TheFreedomStaff = 880,
  Soldier_t_TheBatOuttaHell = 939,
  Soldier_t_TheMemoryMaker = 954,
  Soldier_t_TheHamShank = 1013,
  Soldier_t_TheNecroSmasher = 1123,
  Soldier_t_TheCrossingGuard = 1127,
  Soldier_t_PrinnyMachete = 30758,
  Pyro_m_FlameThrower = 21,
  Pyro_m_FlameThrowerR = 208,
  Pyro_m_TheBackburner = 40,
  Pyro_m_TheDegreaser = 215,
  Pyro_m_ThePhlogistinator = 594,
  Pyro_m_FestiveFlameThrower = 659,
  Pyro_m_TheRainblower = 741,
  Pyro_m_SilverBotkillerFlameThrowerMkI = 798,
  Pyro_m_GoldBotkillerFlameThrowerMkI = 807,
  Pyro_m_RustBotkillerFlameThrowerMkI = 887,
  Pyro_m_BloodBotkillerFlameThrowerMkI = 896,
  Pyro_m_CarbonadoBotkillerFlameThrowerMkI = 905,
  Pyro_m_DiamondBotkillerFlameThrowerMkI = 914,
  Pyro_m_SilverBotkillerFlameThrowerMkII = 963,
  Pyro_m_GoldBotkillerFlameThrowerMkII = 972,
  Pyro_m_FestiveBackburner = 1146,
  Pyro_m_DragonsFury = 1178,
  Pyro_m_ForestFire = 15005,
  Pyro_m_BarnBurner = 15017,
  Pyro_m_BovineBlazemaker = 15030,
  Pyro_m_EarthSkyandFire = 15034,
  Pyro_m_FlashFryer = 15049,
  Pyro_m_TurbineTorcher = 15054,
  Pyro_m_Autumn = 15066,
  Pyro_m_PumpkinPatch = 15067,
  Pyro_m_Nutcracker = 15068,
  Pyro_m_Balloonicorn = 15089,
  Pyro_m_Rainbow = 15090,
  Pyro_m_CoffinNail = 15115,
  Pyro_m_Warhawk = 15141,
  Pyro_m_NostromoNapalmer = 30474,
  Pyro_s_PyrosShotgun = 12,
  Pyro_s_ShotgunR = 199,
  Pyro_s_TheFlareGun = 39,
  Pyro_s_TheDetonator = 351,
  Pyro_s_TheReserveShooter = 415,
  Pyro_s_TheManmelter = 595,
  Pyro_s_TheScorchShot = 740,
  Pyro_s_FestiveFlareGun = 1081,
  Pyro_s_FestiveShotgun = 1141,
  Pyro_s_PanicAttack = 1153,
  Pyro_s_ThermalThruster = 1179,
  Pyro_s_GasPasser = 1180,
  Pyro_s_BackwoodsBoomstick = 15003,
  Pyro_s_RusticRuiner = 15016,
  Pyro_s_CivicDuty = 15044,
  Pyro_s_LightningRod = 15047,
  Pyro_s_Autumn = 15085,
  Pyro_s_FlowerPower = 15109,
  Pyro_s_CoffinNail = 15132,
  Pyro_s_DressedtoKill = 15133,
  Pyro_s_RedBear = 15152,
  Pyro_t_FireAxe = 2,
  Pyro_t_FireAxeR = 192,
  Pyro_t_TheAxtinguisher = 38,
  Pyro_t_Homewrecker = 153,
  Pyro_t_ThePowerjack = 214,
  Pyro_t_TheBackScratcher = 326,
  Pyro_t_SharpenedVolcanoFragment = 348,
  Pyro_t_ThePostalPummeler = 457,
  Pyro_t_TheMaul = 466,
  Pyro_t_TheConscientiousObjector = 474,
  Pyro_t_TheThirdDegree = 593,
  Pyro_t_TheLollichop = 739,
  Pyro_t_NeonAnnihilator = 813,
  Pyro_t_NeonAnnihilatorG = 834,
  Pyro_t_TheFreedomStaff = 880,
  Pyro_t_TheBatOuttaHell = 939,
  Pyro_t_TheMemoryMaker = 954,
  Pyro_t_TheFestiveAxtinguisher = 1000,
  Pyro_t_TheHamShank = 1013,
  Pyro_t_TheNecroSmasher = 1123,
  Pyro_t_TheCrossingGuard = 1127,
  Pyro_t_HotHand = 1181,
  Pyro_t_PrinnyMachete = 30758,
  Demoman_m_GrenadeLauncher = 19,
  Demoman_m_GrenadeLauncherR = 206,
  Demoman_m_TheLochnLoad = 308,
  Demoman_m_AliBabasWeeBooties = 405,
  Demoman_m_TheBootlegger = 608,
  Demoman_m_TheLooseCannon = 996,
  Demoman_m_FestiveGrenadeLauncher = 1007,
  Demoman_m_TheBASEJumper = 1101,
  Demoman_m_TheIronBomber = 1151,
  Demoman_m_Autumn = 15077,
  Demoman_m_MacabreWeb = 15079,
  Demoman_m_Rainbow = 15091,
  Demoman_m_SweetDreams = 15092,
  Demoman_m_CoffinNail = 15116,
  Demoman_m_TopShelf = 15117,
  Demoman_m_Warhawk = 15142,
  Demoman_m_ButcherBird = 15158,
  Demoman_s_StickybombLauncher = 20,
  Demoman_s_StickybombLauncherR = 207,
  Demoman_s_TheScottishResistance = 130,
  Demoman_s_TheCharginTarge = 131,
  Demoman_s_StickyJumper = 265,
  Demoman_s_TheSplendidScreen = 406,
  Demoman_s_FestiveStickybombLauncher = 661,
  Demoman_s_SilverBotkillerStickybombLauncherMkI = 797,
  Demoman_s_GoldBotkillerStickybombLauncherMkI = 806,
  Demoman_s_RustBotkillerStickybombLauncherMkI = 886,
  Demoman_s_BloodBotkillerStickybombLauncherMkI = 895,
  Demoman_s_CarbonadoBotkillerStickybombLauncherMkI = 904,
  Demoman_s_DiamondBotkillerStickybombLauncherMkI = 913,
  Demoman_s_SilverBotkillerStickybombLauncherMkII = 962,
  Demoman_s_GoldBotkillerStickybombLauncherMkII = 971,
  Demoman_s_TheTideTurner = 1099,
  Demoman_s_FestiveTarge = 1144,
  Demoman_s_TheQuickiebombLauncher = 1150,
  Demoman_s_SuddenFlurry = 15009,
  Demoman_s_CarpetBomber = 15012,
  Demoman_s_BlastedBombardier = 15024,
  Demoman_s_RooftopWrangler = 15038,
  Demoman_s_LiquidAsset = 15045,
  Demoman_s_PinkElephant = 15048,
  Demoman_s_Autumn = 15082,
  Demoman_s_PumpkinPatch = 15083,
  Demoman_s_MacabreWeb = 15084,
  Demoman_s_SweetDreams = 15113,
  Demoman_s_CoffinNail = 15137,
  Demoman_s_DressedtoKill = 15138,
  Demoman_s_Blitzkrieg = 15155,
  Demoman_t_Bottle = 1,
  Demoman_t_BottleR = 191,
  Demoman_t_TheEyelander = 132,
  Demoman_t_ThePainTrain = 154,
  Demoman_t_TheScotsmansSkullcutter = 172,
  Demoman_t_HorselessHeadlessHorsemannsHeadtaker = 266,
  Demoman_t_UllapoolCaber = 307,
  Demoman_t_TheClaidheamhMor = 327,
  Demoman_t_TheHalfZatoichi = 357,
  Demoman_t_ThePersianPersuader = 404,
  Demoman_t_TheConscientiousObjector = 474,
  Demoman_t_NessiesNineIron = 482,
  Demoman_t_TheScottishHandshake = 609,
  Demoman_t_TheFreedomStaff = 880,
  Demoman_t_TheBatOuttaHell = 939,
  Demoman_t_TheMemoryMaker = 954,
  Demoman_t_TheHamShank = 1013,
  Demoman_t_FestiveEyelander = 1082,
  Demoman_t_TheNecroSmasher = 1123,
  Demoman_t_TheCrossingGuard = 1127,
  Demoman_t_PrinnyMachete = 30758,
  Heavy_m_Minigun = 15,
  Heavy_m_MinigunR = 202,
  Heavy_m_Natascha = 41,
  Heavy_m_IronCurtain = 298,
  Heavy_m_TheBrassBeast = 312,
  Heavy_m_Tomislav = 424,
  Heavy_m_FestiveMinigun = 654,
  Heavy_m_SilverBotkillerMinigunMkI = 793,
  Heavy_m_GoldBotkillerMinigunMkI = 802,
  Heavy_m_TheHuoLongHeater = 811,
  Heavy_m_TheHuoLongHeaterG = 832,
  Heavy_m_Deflector_mvm = 850,
  Heavy_m_RustBotkillerMinigunMkI = 882,
  Heavy_m_BloodBotkillerMinigunMkI = 891,
  Heavy_m_CarbonadoBotkillerMinigunMkI = 900,
  Heavy_m_DiamondBotkillerMinigunMkI = 909,
  Heavy_m_SilverBotkillerMinigunMkII = 958,
  Heavy_m_GoldBotkillerMinigunMkII = 967,
  Heavy_m_KingoftheJungle = 15004,
  Heavy_m_IronWood = 15020,
  Heavy_m_AntiqueAnnihilator = 15026,
  Heavy_m_WarRoom = 15031,
  Heavy_m_CitizenPain = 15040,
  Heavy_m_BrickHouse = 15055,
  Heavy_m_MacabreWeb = 15086,
  Heavy_m_PumpkinPatch = 15087,
  Heavy_m_Nutcracker = 15088,
  Heavy_m_BrainCandy = 15098,
  Heavy_m_MisterCuddles = 15099,
  Heavy_m_CoffinNail = 15123,
  Heavy_m_DressedtoKill = 15124,
  Heavy_m_TopShelf = 15125,
  Heavy_m_ButcherBird = 15147,
  Heavy_s_HeavysShotgun = 11,
  Heavy_s_ShotgunR = 199,
  Heavy_s_Sandvich = 42,
  Heavy_s_TheDalokohsBar = 159,
  Heavy_s_TheBuffaloSteakSandvich = 311,
  Heavy_s_TheFamilyBusiness = 425,
  Heavy_s_Fishcake = 433,
  Heavy_s_RoboSandvich = 863,
  Heavy_s_FestiveSandvich = 1002,
  Heavy_s_FestiveShotgun = 1141,
  Heavy_s_PanicAttack = 1153,
  Heavy_s_SecondBanana = 1190,
  Heavy_s_BackwoodsBoomstick = 15003,
  Heavy_s_RusticRuiner = 15016,
  Heavy_s_CivicDuty = 15044,
  Heavy_s_LightningRod = 15047,
  Heavy_s_Autumn = 15085,
  Heavy_s_FlowerPower = 15109,
  Heavy_s_CoffinNail = 15132,
  Heavy_s_DressedtoKill = 15133,
  Heavy_s_RedBear = 15152,
  Heavy_t_Fists = 5,
  Heavy_t_FistsR = 195,
  Heavy_t_TheKillingGlovesofBoxing = 43,
  Heavy_t_GlovesofRunningUrgently = 239,
  Heavy_t_WarriorsSpirit = 310,
  Heavy_t_FistsofSteel = 331,
  Heavy_t_TheEvictionNotice = 426,
  Heavy_t_TheConscientiousObjector = 474,
  Heavy_t_ApocoFists = 587,
  Heavy_t_TheHolidayPunch = 656,
  Heavy_t_TheFreedomStaff = 880,
  Heavy_t_TheBatOuttaHell = 939,
  Heavy_t_TheMemoryMaker = 954,
  Heavy_t_TheHamShank = 1013,
  Heavy_t_FestiveGlovesofRunningUrgently = 1084,
  Heavy_t_TheBreadBite = 1100,
  Heavy_t_TheNecroSmasher = 1123,
  Heavy_t_TheCrossingGuard = 1127,
  Heavy_t_GlovesofRunningUrgentlyMvM = 1184,
  Heavy_t_PrinnyMachete = 30758,
  Engi_m_EngineersShotgun = 9,
  Engi_m_ShotgunR = 199,
  Engi_m_TheFrontierJustice = 141,
  Engi_m_TheWidowmaker = 527,
  Engi_m_ThePomson6000 = 588,
  Engi_m_TheRescueRanger = 997,
  Engi_m_FestiveFrontierJustice = 1004,
  Engi_m_FestiveShotgun = 1141,
  Engi_m_PanicAttack = 1153,
  Engi_m_BackwoodsBoomstick = 15003,
  Engi_m_RusticRuiner = 15016,
  Engi_m_CivicDuty = 15044,
  Engi_m_LightningRod = 15047,
  Engi_m_Autumn = 15085,
  Engi_m_FlowerPower = 15109,
  Engi_m_CoffinNail = 15132,
  Engi_m_DressedtoKill = 15133,
  Engi_m_RedBear = 15152,
  Engi_s_EngineersPistol = 22,
  Engi_s_PistolR = 209,
  Engi_s_TheWrangler = 140,
  Engi_s_VintageLugermorph = 160,
  Engi_s_Lugermorph = 294,
  Engi_s_TheShortCircuit = 528,
  Engi_s_FestiveWrangler = 1086,
  Engi_s_RedRockRoscoe = 15013,
  Engi_s_HomemadeHeater = 15018,
  Engi_s_HickoryHolepuncher = 15035,
  Engi_s_LocalHero = 15041,
  Engi_s_BlackDahlia = 15046,
  Engi_s_SandstoneSpecial = 15056,
  Engi_s_MacabreWeb = 15060,
  Engi_s_Nutcracker = 15061,
  Engi_s_BlueMew = 15100,
  Engi_s_BrainCandy = 15101,
  Engi_s_ShottoHell = 15102,
  Engi_s_DressedToKill = 15126,
  Engi_s_Blitzkrieg = 15148,
  Engi_s_TheCAPPER = 30666,
  Engi_s_TheGigarCounter = 30668,
  Engi_t_Wrench = 7,
  Engi_t_WrenchR = 197,
  Engi_t_TheGunslinger = 142,
  Engi_t_TheSouthernHospitality = 155,
  Engi_t_GoldenWrench = 169,
  Engi_t_TheJag = 329,
  Engi_t_TheEurekaEffect = 589,
  Engi_t_FestiveWrench = 662,
  Engi_t_SilverBotkillerWrenchMkI = 795,
  Engi_t_GoldBotkillerWrenchMkI = 804,
  Engi_t_RustBotkillerWrenchMkI = 884,
  Engi_t_BloodBotkillerWrenchMkI = 893,
  Engi_t_CarbonadoBotkillerWrenchMkI = 902,
  Engi_t_DiamondBotkillerWrenchMkI = 911,
  Engi_t_SilverBotkillerWrenchMkII = 960,
  Engi_t_GoldBotkillerWrenchMkII = 969,
  Engi_t_TheNecroSmasher = 1123,
  Engi_t_Nutcracker = 15073,
  Engi_t_Autumn = 15074,
  Engi_t_Boneyard = 15075,
  Engi_t_DressedtoKill = 15139,
  Engi_t_TopShelf = 15140,
  Engi_t_TorquedtoHell = 15114,
  Engi_t_Airwolf = 15156,
  Engi_t_PrinnyMachete = 30758,
  Engi_p_ConstructionPDA = 25,
  Engi_p_ConstructionPDAR = 737,
  Engi_p_DestructionPDA = 26,
  Engi_p_PDA = 28,
  Medic_m_SyringeGun = 17,
  Medic_m_SyringeGunR = 204,
  Medic_m_TheBlutsauger = 36,
  Medic_m_CrusadersCrossbow = 305,
  Medic_m_TheOverdose = 412,
  Medic_m_FestiveCrusadersCrossbow = 1079,
  Medic_s_MediGun = 29,
  Medic_s_MediGunR = 211,
  Medic_s_TheKritzkrieg = 35,
  Medic_s_TheQuickFix = 411,
  Medic_s_FestiveMediGun = 663,
  Medic_s_SilverBotkillerMediGunMkI = 796,
  Medic_s_GoldBotkillerMediGunMkI = 805,
  Medic_s_RustBotkillerMediGunMkI = 885,
  Medic_s_BloodBotkillerMediGunMkI = 894,
  Medic_s_CarbonadoBotkillerMediGunMkI = 903,
  Medic_s_DiamondBotkillerMediGunMkI = 912,
  Medic_s_SilverBotkillerMediGunMkII = 961,
  Medic_s_GoldBotkillerMediGunMkII = 970,
  Medic_s_TheVaccinator = 998,
  Medic_s_MaskedMender = 15008,
  Medic_s_WrappedReviver = 15010,
  Medic_s_ReclaimedReanimator = 15025,
  Medic_s_CivilServant = 15039,
  Medic_s_SparkofLife = 15050,
  Medic_s_Wildwood = 15078,
  Medic_s_FlowerPower = 15097,
  Medic_s_DressedToKill = 15121,
  Medic_s_HighRollers = 15122,
  Medic_s_CoffinNail = 15123,
  Medic_s_Blitzkrieg = 15145,
  Medic_s_Corsair = 15146,
  Medic_t_Bonesaw = 8,
  Medic_t_BonesawR = 198,
  Medic_t_TheUbersaw = 37,
  Medic_t_TheVitaSaw = 173,
  Medic_t_Amputator = 304,
  Medic_t_TheSolemnVow = 413,
  Medic_t_TheConscientiousObjector = 474,
  Medic_t_TheFreedomStaff = 880,
  Medic_t_TheBatOuttaHell = 939,
  Medic_t_TheMemoryMaker = 954,
  Medic_t_FestiveUbersaw = 1003,
  Medic_t_TheHamShank = 1013,
  Medic_t_TheNecroSmasher = 1123,
  Medic_t_TheCrossingGuard = 1127,
  Medic_t_FestiveBonesaw = 1143,
  Medic_t_PrinnyMachete = 30758,
  Sniper_m_SniperRifle = 14,
  Sniper_m_SniperRifleR = 201,
  Sniper_m_TheHuntsman = 56,
  Sniper_m_TheSydneySleeper = 230,
  Sniper_m_TheBazaarBargain = 402,
  Sniper_m_TheMachina = 526,
  Sniper_m_FestiveSniperRifle = 664,
  Sniper_m_TheHitmansHeatmaker = 752,
  Sniper_m_SilverBotkillerSniperRifleMkI = 792,
  Sniper_m_GoldBotkillerSniperRifleMkI = 801,
  Sniper_m_TheAWPerHand = 851,
  Sniper_m_RustBotkillerSniperRifleMkI = 881,
  Sniper_m_BloodBotkillerSniperRifleMkI = 890,
  Sniper_m_CarbonadoBotkillerSniperRifleMkI = 899,
  Sniper_m_DiamondBotkillerSniperRifleMkI = 908,
  Sniper_m_SilverBotkillerSniperRifleMkII = 957,
  Sniper_m_GoldBotkillerSniperRifleMkII = 966,
  Sniper_m_FestiveHuntsman = 1005,
  Sniper_m_TheFortifiedCompound = 1092,
  Sniper_m_TheClassic = 1098,
  Sniper_m_NightOwl = 15000,
  Sniper_m_PurpleRange = 15007,
  Sniper_m_LumberFromDownUnder = 15019,
  Sniper_m_ShotintheDark = 15023,
  Sniper_m_Bogtrotter = 15033,
  Sniper_m_Thunderbolt = 15059,
  Sniper_m_PumpkinPatch = 15070,
  Sniper_m_Boneyard = 15071,
  Sniper_m_Wildwood = 15072,
  Sniper_m_Balloonicorn = 15111,
  Sniper_m_Rainbow = 15112,
  Sniper_m_CoffinNail = 15135,
  Sniper_m_DressedtoKill = 15136,
  Sniper_m_Airwolf = 15154,
  Sniper_m_ShootingStar = 30665,
  Sniper_s_SMG = 16,
  Sniper_s_SMGR = 203,
  Sniper_s_TheRazorback = 57,
  Sniper_s_Jarate = 58,
  Sniper_s_DarwinsDangerShield = 231,
  Sniper_s_CozyCamper = 642,
  Sniper_s_TheCleanersCarbine = 751,
  Sniper_s_FestiveJarate = 1083,
  Sniper_s_TheSelfAwareBeautyMark = 1105,
  Sniper_s_FestiveSMG = 1149,
  Sniper_s_WoodsyWidowmaker = 15001,
  Sniper_s_PlaidPotshotter = 15022,
  Sniper_s_TreadplateTormenter = 15032,
  Sniper_s_TeamSprayer = 15037,
  Sniper_s_LowProfile = 15058,
  Sniper_s_Wildwood = 15076,
  Sniper_s_BlueMew = 15110,
  Sniper_s_HighRollers = 15134,
  Sniper_s_Blitzkrieg = 15153,
  Sniper_t_Kukri = 3,
  Sniper_t_KukriR = 193,
  Sniper_t_TheTribalmansShiv = 171,
  Sniper_t_TheBushwacka = 232,
  Sniper_t_TheShahanshah = 401,
  Sniper_t_TheConscientiousObjector = 474,
  Sniper_t_TheFreedomStaff = 880,
  Sniper_t_TheBatOuttaHell = 939,
  Sniper_t_TheMemoryMaker = 954,
  Sniper_t_TheHamShank = 1013,
  Sniper_t_TheNecroSmasher = 1123,
  Sniper_t_TheCrossingGuard = 1127,
  Sniper_t_PrinnyMachete = 30758,
  Spy_m_Revolver = 24,
  Spy_m_RevolverR = 210,
  Spy_m_TheAmbassador = 61,
  Spy_m_BigKill = 161,
  Spy_m_LEtranger = 224,
  Spy_m_TheEnforcer = 460,
  Spy_m_TheDiamondback = 525,
  Spy_m_FestiveAmbassador = 1006,
  Spy_m_FestiveRevolver = 1142,
  Spy_m_PsychedelicSlugger = 15011,
  Spy_m_OldCountry = 15027,
  Spy_m_Mayor = 15042,
  Spy_m_DeadReckoner = 15051,
  Spy_m_Boneyard = 15062,
  Spy_m_Wildwood = 15063,
  Spy_m_MacabreWeb = 15064,
  Spy_m_FlowerPower = 15103,
  Spy_m_TopShelf = 15128,
  Spy_m_CoffinNail = 15129,
  Spy_m_Blitzkrieg = 15149,
  Spy_s_Sapper = 735,
  Spy_s_SapperR = 736,
  Spy_s_TheRedTapeRecorder = 810,
  Spy_s_TheRedTapeRecorderG = 831,
  Spy_s_TheApSapG = 933,
  Spy_s_FestiveSapper = 1080,
  Spy_s_TheSnackAttack = 1102,
  Spy_t_Knife = 4,
  Spy_t_KnifeR = 194,
  Spy_t_YourEternalReward = 225,
  Spy_t_ConniversKunai = 356,
  Spy_t_TheBigEarner = 461,
  Spy_t_TheWangaPrick = 574,
  Spy_t_TheSharpDresser = 638,
  Spy_t_TheSpycicle = 649,
  Spy_t_FestiveKnife = 665,
  Spy_t_TheBlackRose = 727,
  Spy_t_SilverBotkillerKnifeMkI = 794,
  Spy_t_GoldBotkillerKnifeMkI = 803,
  Spy_t_RustBotkillerKnifeMkI = 883,
  Spy_t_BloodBotkillerKnifeMkI = 892,
  Spy_t_CarbonadoBotkillerKnifeMkI = 901,
  Spy_t_DiamondBotkillerKnifeMkI = 910,
  Spy_t_SilverBotkillerKnifeMkII = 959,
  Spy_t_GoldBotkillerKnifeMkII = 968,
  Spy_t_Boneyard = 15062,
  Spy_t_BlueMew = 15094,
  Spy_t_BrainCandy = 15095,
  Spy_t_StabbedtoHell = 15096,
  Spy_t_DressedtoKill = 15118,
  Spy_t_TopShelf = 15119,
  Spy_t_Blitzkrieg = 15143,
  Spy_t_Airwolf = 15144,
  Spy_t_PrinnyMachete = 30758,
  Spy_d_DisguiseKitPDA = 27,
  Spy_w_InvisWatch = 30,
  Spy_w_InvisWatchR = 212,
  Spy_w_TheDeadRinger = 59,
  Spy_w_TheCloakandDagger = 60,
  Spy_w_EnthusiastsTimepiece = 297,
  Spy_w_TheQuackenbirdt = 947,
  Misc_t_FryingPan = 264,
  Misc_t_GoldFryingPan = 1071,
  Misc_t_Saxxy = 423
};

enum {
  TF_WEAPON_NONE = 0,
  TF_WEAPON_BAT,
  TF_WEAPON_BAT_WOOD,
  TF_WEAPON_BOTTLE, 
  TF_WEAPON_FIREAXE,
  TF_WEAPON_CLUB,
  TF_WEAPON_CROWBAR,
  TF_WEAPON_KNIFE,
  TF_WEAPON_FISTS,
  TF_WEAPON_SHOVEL,
  TF_WEAPON_WRENCH,
  TF_WEAPON_BONESAW,
  TF_WEAPON_SHOTGUN_PRIMARY,
  TF_WEAPON_SHOTGUN_SOLDIER,
  TF_WEAPON_SHOTGUN_HWG,
  TF_WEAPON_SHOTGUN_PYRO,
  TF_WEAPON_SCATTERGUN,
  TF_WEAPON_SNIPERRIFLE,
  TF_WEAPON_MINIGUN,
  TF_WEAPON_SMG,
  TF_WEAPON_SYRINGEGUN_MEDIC,
  TF_WEAPON_TRANQ,
  TF_WEAPON_ROCKETLAUNCHER,
  TF_WEAPON_GRENADELAUNCHER,
  TF_WEAPON_PIPEBOMBLAUNCHER,
  TF_WEAPON_FLAMETHROWER,
  TF_WEAPON_GRENADE_NORMAL,
  TF_WEAPON_GRENADE_CONCUSSION,
  TF_WEAPON_GRENADE_NAIL,
  TF_WEAPON_GRENADE_MIRV,
  TF_WEAPON_GRENADE_MIRV_DEMOMAN,
  TF_WEAPON_GRENADE_NAPALM,
  TF_WEAPON_GRENADE_GAS,
  TF_WEAPON_GRENADE_EMP,
  TF_WEAPON_GRENADE_CALTROP,
  TF_WEAPON_GRENADE_PIPEBOMB,
  TF_WEAPON_GRENADE_SMOKE_BOMB,
  TF_WEAPON_GRENADE_HEAL,
  TF_WEAPON_GRENADE_STUNBALL,
  TF_WEAPON_GRENADE_JAR,
  TF_WEAPON_GRENADE_JAR_MILK,
  TF_WEAPON_PISTOL,
  TF_WEAPON_PISTOL_SCOUT,
  TF_WEAPON_REVOLVER,
  TF_WEAPON_NAILGUN,
  TF_WEAPON_PDA,
  TF_WEAPON_PDA_ENGINEER_BUILD,
  TF_WEAPON_PDA_ENGINEER_DESTROY,
  TF_WEAPON_PDA_SPY,
  TF_WEAPON_BUILDER,
  TF_WEAPON_MEDIGUN,
  TF_WEAPON_GRENADE_MIRVBOMB,
  TF_WEAPON_FLAMETHROWER_ROCKET,
  TF_WEAPON_GRENADE_DEMOMAN,
  TF_WEAPON_SENTRY_BULLET,
  TF_WEAPON_SENTRY_ROCKET,
  TF_WEAPON_DISPENSER,
  TF_WEAPON_INVIS,
  TF_WEAPON_FLAREGUN,
  TF_WEAPON_LUNCHBOX,
  TF_WEAPON_JAR,
  TF_WEAPON_COMPOUND_BOW,
  TF_WEAPON_BUFF_ITEM,
  TF_WEAPON_PUMPKIN_BOMB,
  TF_WEAPON_SWORD, 
  TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT,
  TF_WEAPON_LIFELINE,
  TF_WEAPON_LASER_POINTER,
  TF_WEAPON_DISPENSER_GUN,
  TF_WEAPON_SENTRY_REVENGE,
  TF_WEAPON_JAR_MILK,
  TF_WEAPON_HANDGUN_SCOUT_PRIMARY,
  TF_WEAPON_BAT_FISH,
  TF_WEAPON_CROSSBOW,
  TF_WEAPON_STICKBOMB,
  TF_WEAPON_HANDGUN_SCOUT_SECONDARY,
  TF_WEAPON_SODA_POPPER,
  TF_WEAPON_SNIPERRIFLE_DECAP,
  TF_WEAPON_RAYGUN,
  TF_WEAPON_PARTICLE_CANNON,
  TF_WEAPON_MECHANICAL_ARM,
  TF_WEAPON_DRG_POMSON,
  TF_WEAPON_BAT_GIFTWRAP,
  TF_WEAPON_GRENADE_ORNAMENT_BALL,
  TF_WEAPON_FLAREGUN_REVENGE,
  TF_WEAPON_PEP_BRAWLER_BLASTER,
  TF_WEAPON_CLEAVER,
  TF_WEAPON_GRENADE_CLEAVER,
  TF_WEAPON_STICKY_BALL_LAUNCHER,
  TF_WEAPON_GRENADE_STICKY_BALL,
  TF_WEAPON_SHOTGUN_BUILDING_RESCUE,
  TF_WEAPON_CANNON,
  TF_WEAPON_THROWABLE,
  TF_WEAPON_GRENADE_THROWABLE,
  TF_WEAPON_PDA_SPY_BUILD,
  TF_WEAPON_GRENADE_WATERBALLOON,
  TF_WEAPON_HARVESTER_SAW,
  TF_WEAPON_SPELLBOOK,
  TF_WEAPON_SPELLBOOK_PROJECTILE,
  TF_WEAPON_SNIPERRIFLE_CLASSIC,
  TF_WEAPON_PARACHUTE,
  TF_WEAPON_GRAPPLINGHOOK,
  TF_WEAPON_PASSTIME_GUN,
  TF_WEAPON_CHARGED_SMG,
  TF_WEAPON_COUNT
};

class Weapon : Entity {
public:
  static constexpr int weapon_data_base_offset = 1828;
  static constexpr int weapon_data_stride = 64;
  static constexpr int weapon_data_damage_offset = 0;
  static constexpr int weapon_data_bullets_per_shot_offset = 4;
  static constexpr int weapon_data_spread_offset = 12;
  static constexpr int weapon_data_fire_delay_offset = 24;
  static constexpr int weapon_data_projectile_type_offset = 44;
  static constexpr int weapon_data_ammo_per_shot_offset = 48;
  static constexpr int weapon_data_projectile_speed_offset = 52;
  static constexpr int weapon_data_smack_delay_offset = 56;
  static constexpr int weapon_data_rapid_fire_offset = 60;
  static constexpr int base_weapon_info_offset_from_last_crit_check = -40;
  static constexpr int melee_weapon_info_offset_from_last_crit_check = 240;
  static constexpr int current_attack_is_crit_offset = 22;
  static constexpr int current_crit_is_random_offset = 23;
  static constexpr int current_attack_is_during_demo_charge_offset = 24;

  static auto reload_mode_offset() -> int
  {
    static const int offset = tf2_netvars::find_offset("DT_TFWeaponBase", {"m_iReloadMode"});
    return offset;
  }

  static auto last_crit_check_time_offset() -> int
  {
    static const int offset = tf2_netvars::find_offset("DT_TFWeaponBase", {"m_flLastCritCheckTime"});
    return offset;
  }

  static auto observed_crit_chance_offset() -> int
  {
    static const int offset = tf2_netvars::find_offset("DT_TFWeaponBase", {"m_flObservedCritChance"});
    return offset;
  }

  static auto inspect_stage_offset() -> int
  {
    static const int offset = tf2_netvars::find_offset("DT_TFWeaponBase", {"m_nInspectStage"});
    return offset;
  }

  short get_def_id(void) {
    return *(short*)(this+0xc30+0x90+0x44);
  }

  int get_slot() {
    void** vtable = *(void***)this;
    auto get_slot_fn = (int (*)(void*))vtable[331];
    return get_slot_fn(this);
  }

  bool are_random_crits_enabled() {
    if (convar_system == nullptr) {
      return false;
    }

    static Convar* tf_weapon_criticals = convar_system->find_var("tf_weapon_criticals");
    if (!is_melee()) {
      return tf_weapon_criticals != nullptr && tf_weapon_criticals->get_int() != 0;
    }

    static Convar* tf_weapon_criticals_melee = convar_system->find_var("tf_weapon_criticals_melee");
    const int melee_crits = tf_weapon_criticals_melee != nullptr ? tf_weapon_criticals_melee->get_int() : 1;
    if (melee_crits == 0) {
      return false;
    }

    return melee_crits == 2 || (tf_weapon_criticals != nullptr && tf_weapon_criticals->get_int() != 0);
  }

  bool can_fire_critical_shot(bool headshot = false) {
    void** vtable = *(void***)this;
    auto can_fire_critical_shot_fn = (bool (*)(void*, bool, void*))vtable[497];
    return can_fire_critical_shot_fn(this, headshot, nullptr);
  }

  bool can_fire_random_critical_shot(float crit_chance) {
    void** vtable = *(void***)this;
    auto can_fire_random_critical_shot_fn = (bool (*)(void*, float))vtable[498];
    return can_fire_random_critical_shot_fn(this, crit_chance);
  }

  int get_weapon_id() {
    const auto weapon_def_id = this->get_def_id();

    switch (weapon_def_id) {
    case Heavy_m_Minigun:
    case Heavy_m_MinigunR:
    case Heavy_m_Natascha:
    case Heavy_m_IronCurtain:
    case Heavy_m_TheBrassBeast:
    case Heavy_m_Tomislav:
    case Heavy_m_FestiveMinigun:
    case Heavy_m_SilverBotkillerMinigunMkI:
    case Heavy_m_GoldBotkillerMinigunMkI:
    case Heavy_m_TheHuoLongHeater:
    case Heavy_m_TheHuoLongHeaterG:
    case Heavy_m_Deflector_mvm:
    case Heavy_m_RustBotkillerMinigunMkI:
    case Heavy_m_BloodBotkillerMinigunMkI:
    case Heavy_m_CarbonadoBotkillerMinigunMkI:
    case Heavy_m_DiamondBotkillerMinigunMkI:
    case Heavy_m_SilverBotkillerMinigunMkII:
    case Heavy_m_GoldBotkillerMinigunMkII:
    case Heavy_m_KingoftheJungle:
    case Heavy_m_IronWood:
    case Heavy_m_AntiqueAnnihilator:
    case Heavy_m_WarRoom:
    case Heavy_m_CitizenPain:
    case Heavy_m_BrickHouse:
    case Heavy_m_MacabreWeb:
    case Heavy_m_PumpkinPatch:
    case Heavy_m_Nutcracker:
    case Heavy_m_BrainCandy:
    case Heavy_m_MisterCuddles:
    case Heavy_m_CoffinNail:
    case Heavy_m_DressedtoKill:
    case Heavy_m_TopShelf:
    case Heavy_m_ButcherBird:
      return TF_WEAPON_MINIGUN;
    case Sniper_s_SMG:
    case Sniper_s_SMGR:
    case Sniper_s_FestiveSMG:
      return TF_WEAPON_SMG;
    case Sniper_s_TheCleanersCarbine:
      return TF_WEAPON_CHARGED_SMG;
    default:
      break;
    }

    const bool is_scout_pistol =
      weapon_def_id == Scout_s_ScoutsPistol
      || weapon_def_id == Scout_s_PistolR
      || weapon_def_id == Scout_s_VintageLugermorph
      || weapon_def_id == Scout_s_Lugermorph
      || weapon_def_id == Scout_s_TheWinger
      || weapon_def_id == Scout_s_PrettyBoysPocketPistol
      || weapon_def_id == Scout_s_RedRockRoscoe
      || weapon_def_id == Scout_s_HomemadeHeater
      || weapon_def_id == Scout_s_HickoryHolepuncher
      || weapon_def_id == Scout_s_LocalHero
      || weapon_def_id == Scout_s_BlackDahlia
      || weapon_def_id == Scout_s_SandstoneSpecial
      || weapon_def_id == Scout_s_MacabreWeb
      || weapon_def_id == Scout_s_Nutcracker
      || weapon_def_id == Scout_s_BlueMew
      || weapon_def_id == Scout_s_BrainCandy
      || weapon_def_id == Scout_s_ShottoHell
      || weapon_def_id == Scout_s_DressedToKill
      || weapon_def_id == Scout_s_Blitzkrieg
      || weapon_def_id == Scout_s_TheCAPPER;
    if (is_scout_pistol) {
      return TF_WEAPON_PISTOL_SCOUT;
    }

    const bool is_engineer_pistol =
      weapon_def_id == Engi_s_EngineersPistol
      || weapon_def_id == Engi_s_PistolR
      || weapon_def_id == Engi_s_VintageLugermorph
      || weapon_def_id == Engi_s_Lugermorph
      || weapon_def_id == Engi_s_RedRockRoscoe
      || weapon_def_id == Engi_s_HomemadeHeater
      || weapon_def_id == Engi_s_HickoryHolepuncher
      || weapon_def_id == Engi_s_LocalHero
      || weapon_def_id == Engi_s_BlackDahlia
      || weapon_def_id == Engi_s_SandstoneSpecial
      || weapon_def_id == Engi_s_MacabreWeb
      || weapon_def_id == Engi_s_Nutcracker
      || weapon_def_id == Engi_s_BlueMew
      || weapon_def_id == Engi_s_BrainCandy
      || weapon_def_id == Engi_s_ShottoHell
      || weapon_def_id == Engi_s_DressedToKill
      || weapon_def_id == Engi_s_Blitzkrieg
      || weapon_def_id == Engi_s_TheCAPPER
      || weapon_def_id == Engi_s_TheGigarCounter;
    if (is_engineer_pistol) {
      return TF_WEAPON_PISTOL;
    }

    return TF_WEAPON_NONE;
  }

  int get_type_id(void) {
    return TF_WEAPON_NONE;
  }

  float get_charge_begin_time() {
    static const int offset = tf2_netvars::find_offset("DT_TFPipebombLauncher", {"m_flChargeBeginTime"});
    if (offset <= 0) {
      return 0.0f;
    }

    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + offset);
  }

  float get_detonate_time() {
    static const int offset = tf2_netvars::find_offset("DT_TFGrenadeLauncher", {"m_flDetonateTime"});
    if (offset <= 0) {
      return 0.0f;
    }

    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + offset);
  }

  bool is_melee(void) {
    if (get_slot() == 2) {
      return true;
    }

    switch (this->get_def_id()) {
    case 0:
    case Demoman_t_Bottle:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 37:
    case 38:
    case 44:
    case 132:
    case 142:
    case 153:
    case 154:
    case 155:
    case 171:
    case 172:
    case 173:
    case 190:
    case 191:
    case 192:
    case 193:
    case 194:
    case 195:
    case 196:
    case 197:
    case 214:
    case 221:
    case 225:
    case 264:
    case Demoman_t_HorselessHeadlessHorsemannsHeadtaker:
    case 307:
    case 310:
    case 317:
    case 325:
    case 327:
    case 329:
    case 348:
    case 349:
    case 355:
    case 357:
    case 404:
    case 423:
    case 447:
    case 450:
    case 452:
    case 466:
    case 474:
    case Demoman_t_NessiesNineIron:
    case 572:
    case 587:
    case 589:
    case 593:
    case 609:
    case 638:
    case 648:
    case 649:
    case 656:
    case 660:
    case 739:
    case 775:
    case 813:
    case 832:
    case 880:
    case 939:
    case 954:
    case 999:
    case 1013:
    case 1071:
    case Demoman_t_FestiveEyelander:
    case 1123:
    case 1127:
    case 30758:
      return true;
    default:
      return false;
    }
  }  

  bool is_automatic(void) {
    switch (this->get_weapon_id()) {
    case TF_WEAPON_PISTOL:
    case TF_WEAPON_PISTOL_SCOUT:
    case TF_WEAPON_SMG:
    case TF_WEAPON_MINIGUN:
    case TF_WEAPON_CHARGED_SMG:
      return true;
    }

    return false;
  }
  
  bool is_headshot_weapon(void) {
    int weapon_def_id = this->get_def_id();
    if (weapon_def_id == Spy_m_TheAmbassador          ||
	weapon_def_id == Spy_m_FestiveAmbassador      ||
	weapon_def_id == Sniper_m_SniperRifle         ||
	weapon_def_id == Sniper_m_SniperRifleR        ||
	weapon_def_id == Sniper_m_TheBazaarBargain    ||
	weapon_def_id == Sniper_m_TheHitmansHeatmaker ||
	weapon_def_id == Sniper_m_TheClassic          ||
	weapon_def_id == Sniper_m_FestiveSniperRifle  ||
	weapon_def_id == Sniper_m_BloodBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_CarbonadoBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_DiamondBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_GoldBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_RustBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_SilverBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_GoldBotkillerSniperRifleMkII ||
	weapon_def_id == Sniper_m_SilverBotkillerSniperRifleMkII ||
	weapon_def_id == Sniper_m_ShootingStar ||
	weapon_def_id == Sniper_m_TheAWPerHand ||
	weapon_def_id == Sniper_m_TheMachina)
      {
	return true;
      }

    return false;
  }

  bool is_sniper_rifle(void) {
    int weapon_def_id = this->get_def_id();
    if (weapon_def_id == Sniper_m_SniperRifle         ||
	weapon_def_id == Sniper_m_SniperRifleR        ||
	weapon_def_id == Sniper_m_TheBazaarBargain    ||
	weapon_def_id == Sniper_m_TheHitmansHeatmaker ||
	weapon_def_id == Sniper_m_TheClassic          ||
	weapon_def_id == Sniper_m_FestiveSniperRifle  ||
	weapon_def_id == Sniper_m_BloodBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_CarbonadoBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_DiamondBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_GoldBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_RustBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_SilverBotkillerSniperRifleMkI ||
	weapon_def_id == Sniper_m_GoldBotkillerSniperRifleMkII ||
	weapon_def_id == Sniper_m_SilverBotkillerSniperRifleMkII ||
	weapon_def_id == Sniper_m_ShootingStar ||
	weapon_def_id == Sniper_m_TheAWPerHand ||
	weapon_def_id == Sniper_m_TheMachina   ||
	weapon_def_id == Sniper_m_TheSydneySleeper)
      {
	return true;
      }

    return false;
  }

  bool is_minigun(void) {
    return this->get_weapon_id() == TF_WEAPON_MINIGUN;
  }

  bool is_medigun() {
    switch (get_def_id()) {
    case Medic_s_MediGun:
    case Medic_s_MediGunR:
    case Medic_s_TheKritzkrieg:
    case Medic_s_TheQuickFix:
    case Medic_s_FestiveMediGun:
    case Medic_s_SilverBotkillerMediGunMkI:
    case Medic_s_GoldBotkillerMediGunMkI:
    case Medic_s_RustBotkillerMediGunMkI:
    case Medic_s_BloodBotkillerMediGunMkI:
    case Medic_s_CarbonadoBotkillerMediGunMkI:
    case Medic_s_DiamondBotkillerMediGunMkI:
    case Medic_s_SilverBotkillerMediGunMkII:
    case Medic_s_GoldBotkillerMediGunMkII:
    case Medic_s_TheVaccinator:
    case Medic_s_MaskedMender:
    case Medic_s_WrappedReviver:
    case Medic_s_ReclaimedReanimator:
    case Medic_s_CivilServant:
    case Medic_s_SparkofLife:
    case Medic_s_Wildwood:
    case Medic_s_FlowerPower:
    case Medic_s_DressedToKill:
    case Medic_s_HighRollers:
    case Medic_s_CoffinNail:
    case Medic_s_Blitzkrieg:
    case Medic_s_Corsair:
      return true;
    default:
      return false;
    }
  }

  bool is_vaccinator() {
    return get_def_id() == Medic_s_TheVaccinator;
  }

  float medigun_charge_level() {
    static const int offset = tf2_netvars::find_offset("DT_WeaponMedigun", {"m_flChargeLevel"});
    if (offset <= 0 || !is_medigun()) {
      return 0.0f;
    }

    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + offset);
  }

  Entity* medigun_healing_target() {
    static const int offset = tf2_netvars::find_offset("DT_WeaponMedigun", {"m_hHealingTarget"});
    if (offset <= 0 || entity_list == nullptr || !is_medigun()) {
      return nullptr;
    }

    const int handle = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + offset);
    return entity_list->entity_from_handle(handle);
  }

  bool medigun_is_healing() {
    static const int offset = tf2_netvars::find_offset("DT_WeaponMedigun", {"m_bHealing"});
    if (offset <= 0 || !is_medigun()) {
      return false;
    }

    return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + offset);
  }

  bool medigun_is_releasing_charge() {
    static const int offset = tf2_netvars::find_offset("DT_WeaponMedigun", {"m_bChargeRelease"});
    if (offset <= 0 || !is_medigun()) {
      return false;
    }

    return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + offset);
  }

  int vaccinator_resist_type() {
    static const int offset = tf2_netvars::find_offset("DT_WeaponMedigun", {"m_nChargeResistType"});
    if (offset <= 0 || !is_vaccinator()) {
      return 0;
    }

    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + offset);
  }
  
  bool can_ambassador_headshot() {
    Entity* owner = this->get_owner_entity();
    if (owner == nullptr)
      return false;

    return (owner->get_tickbase() * TICK_INTERVAL) - this->get_last_attack() > 1.f;
  }
  
  float get_charged_damage(void) {
    static const int offset = tf2_netvars::find_offset("DT_TFSniperRifle", {"SniperRifleLocalData", "m_flChargedDamage"});
    constexpr int fallback_offset = 0x109C;
    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + (offset > 0 ? offset : fallback_offset));
  }  

  uintptr_t get_weapon_info_base() {
    const int offset = last_crit_check_time_offset();
    if (offset <= 0) {
      return 0;
    }

    const int info_offset = get_slot() == 2
      ? offset + melee_weapon_info_offset_from_last_crit_check
      : offset + base_weapon_info_offset_from_last_crit_check;
    if (info_offset <= 0) {
      return 0;
    }

    return *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(this) + info_offset);
  }

  uintptr_t get_weapon_data() {
    const uintptr_t weapon_info = get_weapon_info_base();
    const int mode = std::clamp(weapon_mode(), 0, 1);
    if (weapon_info == 0) {
      return 0;
    }

    return weapon_info + weapon_data_base_offset + (static_cast<uintptr_t>(mode) * weapon_data_stride);
  }

  int get_damage() {
    const uintptr_t weapon_data = get_weapon_data();
    if (weapon_data == 0) {
      return 0;
    }

    const int base_damage = *reinterpret_cast<int*>(weapon_data + weapon_data_damage_offset);
    return static_cast<int>(attribute_manager->attrib_hook_value(static_cast<float>(base_damage), "mult_dmg", to_entity()));
  }

  int get_bullets_per_shot() {
    const uintptr_t weapon_data = get_weapon_data();
    if (weapon_data == 0) {
      return 1;
    }

    const int bullets = *reinterpret_cast<int*>(weapon_data + weapon_data_bullets_per_shot_offset);
    if (bullets <= 0 || is_melee()) {
      return 1;
    }

    return std::max(1, static_cast<int>(attribute_manager->attrib_hook_value(static_cast<float>(bullets), "mult_bullets_per_shot", to_entity())));
  }

  float get_hitscan_spread() {
    const uintptr_t weapon_data = get_weapon_data();
    if (weapon_data == 0 || is_melee()) {
      return 0.0f;
    }

    float spread = *reinterpret_cast<float*>(weapon_data + weapon_data_spread_offset);
    if (attribute_manager != nullptr) {
      spread = attribute_manager->attrib_hook_value(spread, "mult_spread_scale", to_entity());
    }

    if (!std::isfinite(spread) || spread <= 0.0f) {
      return 0.0f;
    }

    return std::clamp(spread, 0.0f, 1.0f);
  }

  float get_fire_rate() {
    const uintptr_t weapon_data = get_weapon_data();
    if (weapon_data == 0) {
      return 0.0f;
    }

    return *reinterpret_cast<float*>(weapon_data + weapon_data_fire_delay_offset);
  }

  int get_projectile_type() {
    const uintptr_t weapon_data = get_weapon_data();
    if (weapon_data == 0) {
      return 0;
    }

    return *reinterpret_cast<int*>(weapon_data + weapon_data_projectile_type_offset);
  }

  float get_projectile_speed_from_data() {
    const uintptr_t weapon_data = get_weapon_data();
    if (weapon_data == 0) {
      return 0.0f;
    }

    const float speed = *reinterpret_cast<float*>(weapon_data + weapon_data_projectile_speed_offset);
    return std::isfinite(speed) ? std::clamp(speed, 0.0f, 5000.0f) : 0.0f;
  }

  float get_smack_delay() {
    const uintptr_t weapon_data = get_weapon_data();
    if (weapon_data == 0) {
      return 0.2f;
    }

    const float smack_delay = *reinterpret_cast<float*>(weapon_data + weapon_data_smack_delay_offset);
    return std::isfinite(smack_delay) ? std::clamp(smack_delay, 0.0f, 1.0f) : 0.2f;
  }

  float get_smack_time() {
    const int offset = inspect_stage_offset();
    if (offset <= 0) {
      return -1.0f;
    }

    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + offset + 28);
  }

  bool is_rapid_fire() {
    const uintptr_t weapon_data = get_weapon_data();
    if (weapon_data == 0) {
      return false;
    }

    return *reinterpret_cast<bool*>(weapon_data + weapon_data_rapid_fire_offset);
  }

  float get_mult_crit_chance() {
    return attribute_manager->attrib_hook_value(1.0f, "mult_crit_chance", to_entity());
  }
  
  float get_last_attack(void) {
    return *(float*)(this + 0x1064);
  }
  
  float get_next_primary_attack(void) {
    return *(float*)(this + 0xE94);
  }

  float get_next_secondary_attack(void) {
    return *(float*)(this + 0xE98);
  }

  int get_clip1(void) {
    return *(int*)(this + 0xB80);
  }

  int get_clip2(void) {
    return *(int*)(this + 0xB84);
  }

  int& weapon_mode() {
    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + reload_mode_offset() - 4);
  }

  float& crit_token_bucket() {
    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + reload_mode_offset() - 240);
  }

  int& crit_checks() {
    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + reload_mode_offset() - 236);
  }

  int& crit_seed_requests() {
    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + reload_mode_offset() - 232);
  }

  float& crit_time() {
    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + last_crit_check_time_offset() - 4);
  }

  float& observed_crit_chance() {
    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + observed_crit_chance_offset());
  }

  int& current_seed() {
    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + last_crit_check_time_offset() + 8);
  }

  float& last_rapid_fire_crit_check_time() {
    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + last_crit_check_time_offset() + 12);
  }

  bool& current_attack_is_crit() {
    return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + reload_mode_offset() + current_attack_is_crit_offset);
  }

  bool& current_crit_is_random() {
    return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + reload_mode_offset() + current_crit_is_random_offset);
  }

  bool& current_attack_is_during_demo_charge() {
    return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + reload_mode_offset() + current_attack_is_during_demo_charge_offset);
  }

  bool can_primary_attack() {
    Entity* owner = this->get_owner_entity();
    if (owner == nullptr)
      return false;

    float next_attack = *(float*)(owner + 0x1088);
    float next_primary_attack = this->get_next_primary_attack();
    float time = owner->get_tickbase() * global_vars->interval_per_tick;    
    
    return (next_primary_attack <= time && next_attack <= time); 
  }

  bool can_secondary_attack() {
    Entity* owner = this->get_owner_entity();
    if (owner == nullptr)
      return false;
    
    float next_attack = *(float*)(owner + 0x1088);
    float next_secondary_attack = this->get_next_secondary_attack();
    float time = owner->get_tickbase() * global_vars->interval_per_tick;    
    
    return (next_secondary_attack <= time && next_attack <= time); 
  }

  Entity* to_entity() {
    return reinterpret_cast<Entity*>(this);
  }

};

#endif
