/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file saveload.h Functions/types related to saving and loading games. */

#ifndef SAVELOAD_H
#define SAVELOAD_H

#include "../fileio_type.h"
#include "../strings_type.h"
#include <string>

/** SaveLoad versions
 * Previous savegame versions, the trunk revision where they were
 * introduced and the released version that had that particular
 * savegame version.
 * Up to savegame version 18 there is a minor version as well.
 *
 * Older entries keep their original numbering.
 *
 * Newer entries should use a descriptive labels, numeric version
 * and PR can be added to comment.
 *
 * Note that this list must not be reordered.
 */
enum SaveLoadVersion : uint16 {
	SL_MIN_VERSION,                         ///< First savegame version

	SLV_1,                                  ///<   1.0         0.1.x, 0.2.x
	SLV_2,                                  /**<   2.0         0.3.0
	                                         *     2.1         0.3.1, 0.3.2 */
	SLV_3,                                  ///<   3.x         lost
	SLV_4,                                  /**<   4.0     1
	                                         *     4.1   122   0.3.3, 0.3.4
	                                         *     4.2  1222   0.3.5
	                                         *     4.3  1417
	                                         *     4.4  1426 */

	SLV_5,                                  /**<   5.0  1429
	                                         *     5.1  1440
	                                         *     5.2  1525   0.3.6 */
	SLV_6,                                  /**<   6.0  1721
	                                         *     6.1  1768 */
	SLV_7,                                  ///<   7.0  1770
	SLV_8,                                  ///<   8.0  1786
	SLV_9,                                  ///<   9.0  1909

	SLV_10,                                 ///<  10.0  2030
	SLV_11,                                 /**<  11.0  2033
	                                         *    11.1  2041 */
	SLV_12,                                 ///<  12.1  2046
	SLV_13,                                 ///<  13.1  2080   0.4.0, 0.4.0.1
	SLV_14,                                 ///<  14.0  2441

	SLV_15,                                 ///<  15.0  2499
	SLV_16,                                 /**<  16.0  2817
	                                         *    16.1  3155 */
	SLV_17,                                 /**<  17.0  3212
	                                         *    17.1  3218 */
	SLV_18,                                 ///<  18    3227
	SLV_19,                                 ///<  19    3396

	SLV_20,                                 ///<  20    3403
	SLV_21,                                 ///<  21    3472   0.4.x
	SLV_22,                                 ///<  22    3726
	SLV_23,                                 ///<  23    3915
	SLV_24,                                 ///<  24    4150

	SLV_25,                                 ///<  25    4259
	SLV_26,                                 ///<  26    4466
	SLV_27,                                 ///<  27    4757
	SLV_28,                                 ///<  28    4987
	SLV_29,                                 ///<  29    5070

	SLV_30,                                 ///<  30    5946
	SLV_31,                                 ///<  31    5999
	SLV_32,                                 ///<  32    6001
	SLV_33,                                 ///<  33    6440
	SLV_34,                                 ///<  34    6455

	SLV_35,                                 ///<  35    6602
	SLV_36,                                 ///<  36    6624
	SLV_37,                                 ///<  37    7182
	SLV_38,                                 ///<  38    7195
	SLV_39,                                 ///<  39    7269

	SLV_40,                                 ///<  40    7326
	SLV_41,                                 ///<  41    7348   0.5.x
	SLV_42,                                 ///<  42    7573
	SLV_43,                                 ///<  43    7642
	SLV_44,                                 ///<  44    8144

	SLV_45,                                 ///<  45    8501
	SLV_46,                                 ///<  46    8705
	SLV_47,                                 ///<  47    8735
	SLV_48,                                 ///<  48    8935
	SLV_49,                                 ///<  49    8969

	SLV_50,                                 ///<  50    8973
	SLV_51,                                 ///<  51    8978
	SLV_52,                                 ///<  52    9066
	SLV_53,                                 ///<  53    9316
	SLV_54,                                 ///<  54    9613

	SLV_55,                                 ///<  55    9638
	SLV_56,                                 ///<  56    9667
	SLV_57,                                 ///<  57    9691
	SLV_58,                                 ///<  58    9762
	SLV_59,                                 ///<  59    9779

	SLV_60,                                 ///<  60    9874
	SLV_61,                                 ///<  61    9892
	SLV_62,                                 ///<  62    9905
	SLV_63,                                 ///<  63    9956
	SLV_64,                                 ///<  64   10006

	SLV_65,                                 ///<  65   10210
	SLV_66,                                 ///<  66   10211
	SLV_67,                                 ///<  67   10236
	SLV_68,                                 ///<  68   10266
	SLV_69,                                 ///<  69   10319

	SLV_70,                                 ///<  70   10541
	SLV_71,                                 ///<  71   10567
	SLV_72,                                 ///<  72   10601
	SLV_73,                                 ///<  73   10903
	SLV_74,                                 ///<  74   11030

	SLV_75,                                 ///<  75   11107
	SLV_76,                                 ///<  76   11139
	SLV_77,                                 ///<  77   11172
	SLV_78,                                 ///<  78   11176
	SLV_79,                                 ///<  79   11188

	SLV_80,                                 ///<  80   11228
	SLV_81,                                 ///<  81   11244
	SLV_82,                                 ///<  82   11410
	SLV_83,                                 ///<  83   11589
	SLV_84,                                 ///<  84   11822

	SLV_85,                                 ///<  85   11874
	SLV_86,                                 ///<  86   12042
	SLV_87,                                 ///<  87   12129
	SLV_88,                                 ///<  88   12134
	SLV_89,                                 ///<  89   12160

	SLV_90,                                 ///<  90   12293
	SLV_91,                                 ///<  91   12347
	SLV_92,                                 ///<  92   12381   0.6.x
	SLV_93,                                 ///<  93   12648
	SLV_94,                                 ///<  94   12816

	SLV_95,                                 ///<  95   12924
	SLV_96,                                 ///<  96   13226
	SLV_97,                                 ///<  97   13256
	SLV_98,                                 ///<  98   13375
	SLV_99,                                 ///<  99   13838

	SLV_100,                                ///< 100   13952
	SLV_101,                                ///< 101   14233
	SLV_102,                                ///< 102   14332
	SLV_103,                                ///< 103   14598
	SLV_104,                                ///< 104   14735

	SLV_105,                                ///< 105   14803
	SLV_106,                                ///< 106   14919
	SLV_107,                                ///< 107   15027
	SLV_108,                                ///< 108   15045
	SLV_109,                                ///< 109   15075

	SLV_110,                                ///< 110   15148
	SLV_111,                                ///< 111   15190
	SLV_112,                                ///< 112   15290
	SLV_113,                                ///< 113   15340
	SLV_114,                                ///< 114   15601

	SLV_115,                                ///< 115   15695
	SLV_116,                                ///< 116   15893   0.7.x
	SLV_117,                                ///< 117   16037
	SLV_118,                                ///< 118   16129
	SLV_119,                                ///< 119   16242

	SLV_120,                                ///< 120   16439
	SLV_121,                                ///< 121   16694
	SLV_122,                                ///< 122   16855
	SLV_123,                                ///< 123   16909
	SLV_124,                                ///< 124   16993

	SLV_125,                                ///< 125   17113
	SLV_126,                                ///< 126   17433
	SLV_127,                                ///< 127   17439
	SLV_128,                                ///< 128   18281
	SLV_129,                                ///< 129   18292

	SLV_130,                                ///< 130   18404
	SLV_131,                                ///< 131   18481
	SLV_132,                                ///< 132   18522
	SLV_133,                                ///< 133   18674
	SLV_134,                                ///< 134   18703

	SLV_135,                                ///< 135   18719
	SLV_136,                                ///< 136   18764
	SLV_137,                                ///< 137   18912
	SLV_138,                                ///< 138   18942   1.0.x
	SLV_139,                                ///< 139   19346

	SLV_140,                                ///< 140   19382
	SLV_141,                                ///< 141   19799
	SLV_142,                                ///< 142   20003
	SLV_143,                                ///< 143   20048
	SLV_144,                                ///< 144   20334

	SLV_145,                                ///< 145   20376
	SLV_146,                                ///< 146   20446
	SLV_147,                                ///< 147   20621
	SLV_148,                                ///< 148   20659
	SLV_149,                                ///< 149   20832

	SLV_150,                                ///< 150   20857
	SLV_151,                                ///< 151   20918
	SLV_152,                                ///< 152   21171
	SLV_153,                                ///< 153   21263
	SLV_154,                                ///< 154   21426

	SLV_155,                                ///< 155   21453
	SLV_156,                                ///< 156   21728
	SLV_157,                                ///< 157   21862
	SLV_158,                                ///< 158   21933
	SLV_159,                                ///< 159   21962

	SLV_160,                                ///< 160   21974   1.1.x
	SLV_161,                                ///< 161   22567
	SLV_162,                                ///< 162   22713
	SLV_163,                                ///< 163   22767
	SLV_164,                                ///< 164   23290

	SLV_165,                                ///< 165   23304
	SLV_166,                                ///< 166   23415
	SLV_167,                                ///< 167   23504
	SLV_168,                                ///< 168   23637
	SLV_169,                                ///< 169   23816

	SLV_170,                                ///< 170   23826
	SLV_171,                                ///< 171   23835
	SLV_172,                                ///< 172   23947
	SLV_173,                                ///< 173   23967   1.2.0-RC1
	SLV_174,                                ///< 174   23973   1.2.x

	SLV_175,                                ///< 175   24136
	SLV_176,                                ///< 176   24446
	SLV_177,                                ///< 177   24619
	SLV_178,                                ///< 178   24789
	SLV_179,                                ///< 179   24810

	SLV_180,                                ///< 180   24998   1.3.x
	SLV_181,                                ///< 181   25012
	SLV_182,                                ///< 182   25115 FS#5492, r25259, r25296 Goal status
	SLV_183,                                ///< 183   25363 Cargodist
	SLV_184,                                ///< 184   25508 Unit localisation split

	SLV_185,                                ///< 185   25620 Storybooks
	SLV_186,                                ///< 186   25833 Objects storage
	SLV_187,                                ///< 187   25899 Linkgraph - restricted flows
	SLV_188,                                ///< 188   26169 v1.4  FS#5831 Unify RV travel time
	SLV_189,                                ///< 189   26450 Hierarchical vehicle subgroups

	SLV_190,                                ///< 190   26547 Separate order travel and wait times
	SLV_191,                                ///< 191   26636 FS#6026 Fix disaster vehicle storage (No bump)
	                                        ///< 191   26646 FS#6041 Linkgraph - store locations
	SLV_192,                                ///< 192   26700 FS#6066 Fix saving of order backups
	SLV_193,                                ///< 193   26802
	SLV_194,                                ///< 194   26881 v1.5

	SLV_195,                                ///< 195   27572 v1.6.1
	SLV_196,                                ///< 196   27778 v1.7
	SLV_197,                                ///< 197   27978 v1.8
	SLV_198,                                ///< 198  PR#6763 Switch town growth rate and counter to actual game ticks
	SLV_EXTEND_CARGOTYPES,                  ///< 199  PR#6802 Extend cargotypes to 64

	SLV_EXTEND_RAILTYPES,                   ///< 200  PR#6805 Extend railtypes to 64, adding uint16 to map array.
	SLV_EXTEND_PERSISTENT_STORAGE,          ///< 201  PR#6885 Extend NewGRF persistent storages.
	SLV_EXTEND_INDUSTRY_CARGO_SLOTS,        ///< 202  PR#6867 Increase industry cargo slots to 16 in, 16 out
	SLV_SHIP_PATH_CACHE,                    ///< 203  PR#7072 Add path cache for ships
	SLV_SHIP_ROTATION,                      ///< 204  PR#7065 Add extra rotation stages for ships.

	SLV_GROUP_LIVERIES,                     ///< 205  PR#7108 Livery storage change and group liveries.
	SLV_SHIPS_STOP_IN_LOCKS,                ///< 206  PR#7150 Ship/lock movement changes.
	SLV_FIX_CARGO_MONITOR,                  ///< 207  PR#7175 v1.9  Cargo monitor data packing fix to support 64 cargotypes.
	SLV_TOWN_CARGOGEN,                      ///< 208  PR#6965 New algorithms for town building cargo generation.
	SLV_SHIP_CURVE_PENALTY,                 ///< 209  PR#7289 Configurable ship curve penalties.

	SLV_SERVE_NEUTRAL_INDUSTRIES,           ///< 210  PR#7234 Company stations can serve industries with attached neutral stations.
	SLV_ROADVEH_PATH_CACHE,                 ///< 211  PR#7261 Add path cache for road vehicles.
	SLV_REMOVE_OPF,                         ///< 212  PR#7245 Remove OPF.
	SLV_TREES_WATER_CLASS,                  ///< 213  PR#7405 WaterClass update for tree tiles.
	SLV_ROAD_TYPES,                         ///< 214  PR#6811 NewGRF road types.

	SLV_SCRIPT_MEMLIMIT,                    ///< 215  PR#7516 Limit on AI/GS memory consumption.
	SLV_MULTITILE_DOCKS,                    ///< 216  PR#7380 Multiple docks per station.
	SLV_TRADING_AGE,                        ///< 217  PR#7780 Configurable company trading age.
	SLV_ENDING_YEAR,                        ///< 218  PR#7747 v1.10 Configurable ending year.
	SLV_REMOVE_TOWN_CARGO_CACHE,            ///< 219  PR#8258 Remove town cargo acceptance and production caches.

	/* Patchpacks for a while considered it a good idea to jump a few versions
	 * above our version for their savegames. But as time continued, this gap
	 * has been closing, up to the point we would start to reuse versions from
	 * their patchpacks. This is not a problem from our perspective: the
	 * savegame will simply fail to load because they all contain chunks we
	 * cannot digest. But, this gives for ugly errors. As we have plenty of
	 * versions anyway, we simply skip the versions we know belong to
	 * patchpacks. This way we can present the user with a clean error
	 * indicate he is loading a savegame from a patchpack.
	 * For future patchpack creators: please follow a system like JGRPP, where
	 * the version is masked with 0x8000, and the true version is stored in
	 * its own chunk with feature toggles.
	 */
	SLV_START_PATCHPACKS,                   ///< 220  First known patchpack to use a version just above ours.
	SLV_END_PATCHPACKS = 286,               ///< 286  Last known patchpack to use a version just above ours.

	SLV_GS_INDUSTRY_CONTROL,                ///< 287  PR#7912 and PR#8115 GS industry control.
	SLV_VEH_MOTION_COUNTER,                 ///< 288  PR#8591 Desync safe motion counter
	SLV_INDUSTRY_TEXT,                      ///< 289  PR#8576 Additional GS text for industries.
	SLV_NEWGRF_ROADSTOPS,                   ///< 290  PR#7955 Add NewGRF Roadstops

	SL_MAX_VERSION,                         ///< Highest possible saveload version
};

/** Save or load result codes. */
enum SaveOrLoadResult {
	SL_OK     = 0, ///< completed successfully
	SL_ERROR  = 1, ///< error that was caught before internal structures were modified
	SL_REINIT = 2, ///< error that was caught in the middle of updating game state, need to clear it. (can only happen during load)
};

/** Deals with the type of the savegame, independent of extension */
struct FileToSaveLoad {
	SaveLoadOperation file_op;           ///< File operation to perform.
	DetailedFileType detail_ftype;   ///< Concrete file type (PNG, BMP, old save, etc).
	AbstractFileType abstract_ftype; ///< Abstract type of file (scenario, heightmap, etc).
	std::string name;                ///< Name of the file.
	char title[255];                 ///< Internal name of the game.

	void SetMode(FiosType ft);
	void SetMode(SaveLoadOperation fop, AbstractFileType aft, DetailedFileType dft);
	void SetName(const char *name);
	void SetTitle(const char *title);
};

/** Types of save games. */
enum SavegameType {
	SGT_TTD,    ///< TTD  savegame (can be detected incorrectly)
	SGT_TTDP1,  ///< TTDP savegame ( -//- ) (data at NW border)
	SGT_TTDP2,  ///< TTDP savegame in new format (data at SE border)
	SGT_OTTD,   ///< OTTD savegame
	SGT_TTO,    ///< TTO savegame
	SGT_INVALID = 0xFF, ///< broken savegame (used internally)
};

extern FileToSaveLoad _file_to_saveload;

void GenerateDefaultSaveName(char *buf, const char *last);
void SetSaveLoadError(StringID str);
const char *GetSaveLoadErrorString();
SaveOrLoadResult SaveOrLoad(const std::string &filename, SaveLoadOperation fop, DetailedFileType dft, Subdirectory sb, bool threaded = true);
void WaitTillSaved();
void ProcessAsyncSaveFinish();
void DoExitSave();

SaveOrLoadResult SaveWithFilter(struct SaveFilter *writer, bool threaded);
SaveOrLoadResult LoadWithFilter(struct LoadFilter *reader);

typedef void ChunkSaveLoadProc();
typedef void AutolengthProc(void *arg);

/** Handlers and description of chunk. */
struct ChunkHandler {
	uint32 id;                          ///< Unique ID (4 letters).
	ChunkSaveLoadProc *save_proc;       ///< Save procedure of the chunk.
	ChunkSaveLoadProc *load_proc;       ///< Load procedure of the chunk.
	ChunkSaveLoadProc *ptrs_proc;       ///< Manipulate pointers in the chunk.
	ChunkSaveLoadProc *load_check_proc; ///< Load procedure for game preview.
	uint32 flags;                       ///< Flags of the chunk. @see ChunkType
};

/** Type of reference (#SLE_REF, #SLE_CONDREF). */
enum SLRefType {
	REF_ORDER          =  0, ///< Load/save a reference to an order.
	REF_VEHICLE        =  1, ///< Load/save a reference to a vehicle.
	REF_STATION        =  2, ///< Load/save a reference to a station.
	REF_TOWN           =  3, ///< Load/save a reference to a town.
	REF_VEHICLE_OLD    =  4, ///< Load/save an old-style reference to a vehicle (for pre-4.4 savegames).
	REF_ROADSTOPS      =  5, ///< Load/save a reference to a bus/truck stop.
	REF_ENGINE_RENEWS  =  6, ///< Load/save a reference to an engine renewal (autoreplace).
	REF_CARGO_PACKET   =  7, ///< Load/save a reference to a cargo packet.
	REF_ORDERLIST      =  8, ///< Load/save a reference to an orderlist.
	REF_STORAGE        =  9, ///< Load/save a reference to a persistent storage.
	REF_LINK_GRAPH     = 10, ///< Load/save a reference to a link graph.
	REF_LINK_GRAPH_JOB = 11, ///< Load/save a reference to a link graph job.
};

/** Flags of a chunk. */
enum ChunkType {
	CH_RIFF         =  0,
	CH_ARRAY        =  1,
	CH_SPARSE_ARRAY =  2,
	CH_TYPE_MASK    =  3,
	CH_LAST         =  8, ///< Last chunk in this array.
};

/**
 * VarTypes is the general bitmasked magic type that tells us
 * certain characteristics about the variable it refers to. For example
 * SLE_FILE_* gives the size(type) as it would be in the savegame and
 * SLE_VAR_* the size(type) as it is in memory during runtime. These are
 * the first 8 bits (0-3 SLE_FILE, 4-7 SLE_VAR).
 * Bits 8-15 are reserved for various flags as explained below
 */
enum VarTypes {
	/* 4 bits allocated a maximum of 16 types for NumberType */
	SLE_FILE_I8       = 0,
	SLE_FILE_U8       = 1,
	SLE_FILE_I16      = 2,
	SLE_FILE_U16      = 3,
	SLE_FILE_I32      = 4,
	SLE_FILE_U32      = 5,
	SLE_FILE_I64      = 6,
	SLE_FILE_U64      = 7,
	SLE_FILE_STRINGID = 8, ///< StringID offset into strings-array
	SLE_FILE_STRING   = 9,
	/* 6 more possible file-primitives */

	/* 4 bits allocated a maximum of 16 types for NumberType */
	SLE_VAR_BL    =  0 << 4,
	SLE_VAR_I8    =  1 << 4,
	SLE_VAR_U8    =  2 << 4,
	SLE_VAR_I16   =  3 << 4,
	SLE_VAR_U16   =  4 << 4,
	SLE_VAR_I32   =  5 << 4,
	SLE_VAR_U32   =  6 << 4,
	SLE_VAR_I64   =  7 << 4,
	SLE_VAR_U64   =  8 << 4,
	SLE_VAR_NULL  =  9 << 4, ///< useful to write zeros in savegame.
	SLE_VAR_STRB  = 10 << 4, ///< string (with pre-allocated buffer)
	SLE_VAR_STRBQ = 11 << 4, ///< string enclosed in quotes (with pre-allocated buffer)
	SLE_VAR_STR   = 12 << 4, ///< string pointer
	SLE_VAR_STRQ  = 13 << 4, ///< string pointer enclosed in quotes
	SLE_VAR_NAME  = 14 << 4, ///< old custom name to be converted to a char pointer
	/* 1 more possible memory-primitives */

	/* Shortcut values */
	SLE_VAR_CHAR = SLE_VAR_I8,

	/* Default combinations of variables. As savegames change, so can variables
	 * and thus it is possible that the saved value and internal size do not
	 * match and you need to specify custom combo. The defaults are listed here */
	SLE_BOOL         = SLE_FILE_I8  | SLE_VAR_BL,
	SLE_INT8         = SLE_FILE_I8  | SLE_VAR_I8,
	SLE_UINT8        = SLE_FILE_U8  | SLE_VAR_U8,
	SLE_INT16        = SLE_FILE_I16 | SLE_VAR_I16,
	SLE_UINT16       = SLE_FILE_U16 | SLE_VAR_U16,
	SLE_INT32        = SLE_FILE_I32 | SLE_VAR_I32,
	SLE_UINT32       = SLE_FILE_U32 | SLE_VAR_U32,
	SLE_INT64        = SLE_FILE_I64 | SLE_VAR_I64,
	SLE_UINT64       = SLE_FILE_U64 | SLE_VAR_U64,
	SLE_CHAR         = SLE_FILE_I8  | SLE_VAR_CHAR,
	SLE_STRINGID     = SLE_FILE_STRINGID | SLE_VAR_U32,
	SLE_STRINGBUF    = SLE_FILE_STRING   | SLE_VAR_STRB,
	SLE_STRINGBQUOTE = SLE_FILE_STRING   | SLE_VAR_STRBQ,
	SLE_STRING       = SLE_FILE_STRING   | SLE_VAR_STR,
	SLE_STRINGQUOTE  = SLE_FILE_STRING   | SLE_VAR_STRQ,
	SLE_NAME         = SLE_FILE_STRINGID | SLE_VAR_NAME,

	/* Shortcut values */
	SLE_UINT  = SLE_UINT32,
	SLE_INT   = SLE_INT32,
	SLE_STRB  = SLE_STRINGBUF,
	SLE_STRBQ = SLE_STRINGBQUOTE,
	SLE_STR   = SLE_STRING,
	SLE_STRQ  = SLE_STRINGQUOTE,

	/* 8 bits allocated for a maximum of 8 flags
	 * Flags directing saving/loading of a variable */
	SLF_NOT_IN_SAVE     = 1 <<  8, ///< do not save with savegame, basically client-based
	SLF_NOT_IN_CONFIG   = 1 <<  9, ///< do not save to config file
	SLF_NO_NETWORK_SYNC = 1 << 10, ///< do not synchronize over network (but it is saved if SLF_NOT_IN_SAVE is not set)
	SLF_ALLOW_CONTROL   = 1 << 11, ///< allow control codes in the strings
	SLF_ALLOW_NEWLINE   = 1 << 12, ///< allow new lines in the strings
	SLF_HEX             = 1 << 13, ///< print numbers as hex in the config file (only useful for unsigned)
	/* 2 more possible flags */
};

typedef uint32 VarType;

/** Type of data saved. */
enum SaveLoadType : byte {
	SL_VAR         =  0, ///< Save/load a variable.
	SL_REF         =  1, ///< Save/load a reference.
	SL_ARR         =  2, ///< Save/load an array.
	SL_STR         =  3, ///< Save/load a string.
	SL_LST         =  4, ///< Save/load a list.
	SL_DEQUE       =  5, ///< Save/load a deque.
	SL_STDSTR      =  6, ///< Save/load a \c std::string.
	/* non-normal save-load types */
	SL_WRITEBYTE   =  8,
	SL_VEH_INCLUDE =  9,
	SL_ST_INCLUDE  = 10,
	SL_END         = 15
};

typedef void *SaveLoadAddrProc(void *base, size_t extra);

/** SaveLoad type struct. Do NOT use this directly but use the SLE_ macros defined just below! */
struct SaveLoad {
	SaveLoadType cmd;    ///< the action to take with the saved/loaded type, All types need different action
	VarType conv;        ///< type of the variable to be saved, int
	uint16 length;       ///< (conditional) length of the variable (eg. arrays) (max array size is 65536 elements)
	SaveLoadVersion version_from;   ///< save/load the variable starting from this savegame version
	SaveLoadVersion version_to;     ///< save/load the variable until this savegame version
	size_t size;                    ///< the sizeof size.
	SaveLoadAddrProc *address_proc; ///< callback proc the get the actual variable address in memory
	size_t extra_data;              ///< extra data for the callback proc
};

/** Same as #SaveLoad but global variables are used (for better readability); */
typedef SaveLoad SaveLoadGlobVarList;

/**
 * Storage of simple variables, references (pointers), and arrays.
 * @param cmd      Load/save type. @see SaveLoadType
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extra    Extra data to pass to the address callback function.
 * @note In general, it is better to use one of the SLE_* macros below.
 */
#define SLE_GENERAL(cmd, base, variable, type, length, from, to, extra) {cmd, type, length, from, to, cpp_sizeof(base, variable), [] (void *b, size_t) -> void * { assert(b != nullptr); return const_cast<void *>(static_cast<const void *>(std::addressof(static_cast<base *>(b)->variable))); }, extra}

/**
 * Storage of a variable in some savegame versions.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 */
#define SLE_CONDVAR(base, variable, type, from, to) SLE_GENERAL(SL_VAR, base, variable, type, 0, from, to, 0)

/**
 * Storage of a reference in some savegame versions.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Type of the reference, a value from #SLRefType.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 */
#define SLE_CONDREF(base, variable, type, from, to) SLE_GENERAL(SL_REF, base, variable, type, 0, from, to, 0)

/**
 * Storage of an array in some savegame versions.
 * @param base     Name of the class or struct containing the array.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 * @param from     First savegame version that has the array.
 * @param to       Last savegame version that has the array.
 */
#define SLE_CONDARR(base, variable, type, length, from, to) SLE_GENERAL(SL_ARR, base, variable, type, length, from, to, 0)

/**
 * Storage of a string in some savegame versions.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the string (only used for fixed size buffers).
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 */
#define SLE_CONDSTR(base, variable, type, length, from, to) SLE_GENERAL(SL_STR, base, variable, type, length, from, to, 0)

/**
 * Storage of a \c std::string in some savegame versions.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 */
#define SLE_CONDSSTR(base, variable, type, from, to) SLE_GENERAL(SL_STDSTR, base, variable, type, 0, from, to, 0)

/**
 * Storage of a list in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLE_CONDLST(base, variable, type, from, to) SLE_GENERAL(SL_LST, base, variable, type, 0, from, to, 0)

/**
 * Storage of a deque in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLE_CONDDEQUE(base, variable, type, from, to) SLE_GENERAL(SL_DEQUE, base, variable, type, 0, from, to, 0)

/**
 * Storage of a variable in every version of a savegame.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_VAR(base, variable, type) SLE_CONDVAR(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a reference in every version of a savegame.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Type of the reference, a value from #SLRefType.
 */
#define SLE_REF(base, variable, type) SLE_CONDREF(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of an array in every version of a savegame.
 * @param base     Name of the class or struct containing the array.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 */
#define SLE_ARR(base, variable, type, length) SLE_CONDARR(base, variable, type, length, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a string in every savegame version.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the string (only used for fixed size buffers).
 */
#define SLE_STR(base, variable, type, length) SLE_CONDSTR(base, variable, type, length, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a \c std::string in every savegame version.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_SSTR(base, variable, type) SLE_CONDSSTR(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a list in every savegame version.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_LST(base, variable, type) SLE_CONDLST(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Empty space in every savegame version.
 * @param length Length of the empty space.
 */
#define SLE_NULL(length) SLE_CONDNULL(length, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Empty space in some savegame versions.
 * @param length Length of the empty space.
 * @param from   First savegame version that has the empty space.
 * @param to     Last savegame version that has the empty space.
 */
#define SLE_CONDNULL(length, from, to) {SL_ARR, SLE_FILE_U8 | SLE_VAR_NULL | SLF_NOT_IN_CONFIG, length, from, to, 0, nullptr, 0}

/** Translate values ingame to different values in the savegame and vv. */
#define SLE_WRITEBYTE(base, variable) SLE_GENERAL(SL_WRITEBYTE, base, variable, 0, 0, SL_MIN_VERSION, SL_MAX_VERSION, 0)

#define SLE_VEH_INCLUDE() {SL_VEH_INCLUDE, 0, 0, SL_MIN_VERSION, SL_MAX_VERSION, 0, [] (void *b, size_t) { return b; }, 0}
#define SLE_ST_INCLUDE() {SL_ST_INCLUDE, 0, 0, SL_MIN_VERSION, SL_MAX_VERSION, 0, [] (void *b, size_t) { return b; }, 0}

/** End marker of a struct/class save or load. */
#define SLE_END() {SL_END, 0, 0, SL_MIN_VERSION, SL_MIN_VERSION, 0, nullptr, 0}

/**
 * Storage of global simple variables, references (pointers), and arrays.
 * @param cmd      Load/save type. @see SaveLoadType
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extra    Extra data to pass to the address callback function.
 * @note In general, it is better to use one of the SLEG_* macros below.
 */
#define SLEG_GENERAL(cmd, variable, type, length, from, to, extra) {cmd, type, length, from, to, sizeof(variable), [] (void *, size_t) -> void * { return static_cast<void *>(std::addressof(variable)); }, extra}

/**
 * Storage of a global variable in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 */
#define SLEG_CONDVAR(variable, type, from, to) SLEG_GENERAL(SL_VAR, variable, type, 0, from, to, 0)

/**
 * Storage of a global reference in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 */
#define SLEG_CONDREF(variable, type, from, to) SLEG_GENERAL(SL_REF, variable, type, 0, from, to, 0)

/**
 * Storage of a global array in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 * @param from     First savegame version that has the array.
 * @param to       Last savegame version that has the array.
 */
#define SLEG_CONDARR(variable, type, length, from, to) SLEG_GENERAL(SL_ARR, variable, type, length, from, to, 0)

/**
 * Storage of a global string in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the string (only used for fixed size buffers).
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 */
#define SLEG_CONDSTR(variable, type, length, from, to) SLEG_GENERAL(SL_STR, variable, type, length, from, to, 0)

/**
 * Storage of a global \c std::string in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 */
#define SLEG_CONDSSTR(variable, type, from, to) SLEG_GENERAL(SL_STDSTR, variable, type, 0, from, to, 0)

/**
 * Storage of a global list in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLEG_CONDLST(variable, type, from, to) SLEG_GENERAL(SL_LST, variable, type, 0, from, to, 0)

/**
 * Storage of a global variable in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_VAR(variable, type) SLEG_CONDVAR(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global reference in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_REF(variable, type) SLEG_CONDREF(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global array in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_ARR(variable, type) SLEG_CONDARR(variable, type, lengthof(variable), SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global string in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_STR(variable, type) SLEG_CONDSTR(variable, type, sizeof(variable), SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global \c std::string in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_SSTR(variable, type) SLEG_CONDSSTR(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global list in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_LST(variable, type) SLEG_CONDLST(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Empty global space in some savegame versions.
 * @param length Length of the empty space.
 * @param from   First savegame version that has the empty space.
 * @param to     Last savegame version that has the empty space.
 */
#define SLEG_CONDNULL(length, from, to) {SL_ARR, SLE_FILE_U8 | SLE_VAR_NULL | SLF_NOT_IN_CONFIG, length, from, to, 0, nullptr, 0}

/** End marker of global variables save or load. */
#define SLEG_END() {SL_END, 0, 0, SL_MIN_VERSION, SL_MIN_VERSION, 0, nullptr, 0}

/**
 * Checks whether the savegame is below \a major.\a minor.
 * @param major Major number of the version to check against.
 * @param minor Minor number of the version to check against. If \a minor is 0 or not specified, only the major number is checked.
 * @return Savegame version is earlier than the specified version.
 */
static inline bool IsSavegameVersionBefore(SaveLoadVersion major, byte minor = 0)
{
	extern SaveLoadVersion _sl_version;
	extern byte            _sl_minor_version;
	return _sl_version < major || (minor > 0 && _sl_version == major && _sl_minor_version < minor);
}

/**
 * Checks whether the savegame is below or at \a major. This should be used to repair data from existing
 * savegames which is no longer corrupted in new savegames, but for which otherwise no savegame
 * bump is required.
 * @param major Major number of the version to check against.
 * @return Savegame version is at most the specified version.
 */
static inline bool IsSavegameVersionUntil(SaveLoadVersion major)
{
	extern SaveLoadVersion _sl_version;
	return _sl_version <= major;
}

/**
 * Checks if some version from/to combination falls within the range of the
 * active savegame version.
 * @param version_from Inclusive savegame version lower bound.
 * @param version_to   Exclusive savegame version upper bound. SL_MAX_VERSION if no upper bound.
 * @return Active savegame version falls within the given range.
 */
static inline bool SlIsObjectCurrentlyValid(SaveLoadVersion version_from, SaveLoadVersion version_to)
{
	extern const SaveLoadVersion SAVEGAME_VERSION;
	if (SAVEGAME_VERSION < version_from || SAVEGAME_VERSION >= version_to) return false;

	return true;
}

/**
 * Get the NumberType of a setting. This describes the integer type
 * as it is represented in memory
 * @param type VarType holding information about the variable-type
 * @return the SLE_VAR_* part of a variable-type description
 */
static inline VarType GetVarMemType(VarType type)
{
	return type & 0xF0; // GB(type, 4, 4) << 4;
}

/**
 * Get the FileType of a setting. This describes the integer type
 * as it is represented in a savegame/file
 * @param type VarType holding information about the file-type
 * @return the SLE_FILE_* part of a variable-type description
 */
static inline VarType GetVarFileType(VarType type)
{
	return type & 0xF; // GB(type, 0, 4);
}

/**
 * Check if the given saveload type is a numeric type.
 * @param conv the type to check
 * @return True if it's a numeric type.
 */
static inline bool IsNumericType(VarType conv)
{
	return GetVarMemType(conv) <= SLE_VAR_U64;
}

/**
 * Get the address of the variable. Null-variables don't have an address,
 * everything else has a callback function that returns the address based
 * on the saveload data and the current object for non-globals.
 */
static inline void *GetVariableAddress(const void *object, const SaveLoad *sld)
{
	/* Entry is a null-variable, mostly used to read old savegames etc. */
	if (GetVarMemType(sld->conv) == SLE_VAR_NULL) {
		assert(sld->address_proc == nullptr);
		return nullptr;
	}

	/* Everything else should be a non-null pointer. */
	assert(sld->address_proc != nullptr);
	return sld->address_proc(const_cast<void *>(object), sld->extra_data);
}

int64 ReadValue(const void *ptr, VarType conv);
void WriteValue(void *ptr, VarType conv, int64 val);

void SlSetArrayIndex(uint index);
int SlIterateArray();

void SlAutolength(AutolengthProc *proc, void *arg);
size_t SlGetFieldLength();
void SlSetLength(size_t length);
size_t SlCalcObjMemberLength(const void *object, const SaveLoad *sld);
size_t SlCalcObjLength(const void *object, const SaveLoad *sld);

byte SlReadByte();
void SlWriteByte(byte b);

void SlGlobList(const SaveLoadGlobVarList *sldg);
void SlArray(void *array, size_t length, VarType conv);
void SlObject(void *object, const SaveLoad *sld);
bool SlObjectMember(void *object, const SaveLoad *sld);
void NORETURN SlError(StringID string, const char *extra_msg = nullptr);
void NORETURN SlErrorCorrupt(const char *msg);
void NORETURN SlErrorCorruptFmt(const char *format, ...) WARN_FORMAT(1, 2);

bool SaveloadCrashWithMissingNewGRFs();

/**
 * Read in bytes from the file/data structure but don't do
 * anything with them, discarding them in effect
 * @param length The amount of bytes that is being treated this way
 */
static inline void SlSkipBytes(size_t length)
{
	for (; length != 0; length--) SlReadByte();
}

extern char _savegame_format[8];
extern bool _do_autosave;

#endif /* SAVELOAD_H */
