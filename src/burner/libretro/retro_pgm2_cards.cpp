// PGM2 Memory card selection for libretro: scan 'g_save_dir'/fbneo/pgm2_memcards/<drv>_pN_*.pg2|.bin

#ifndef NO_PGM2

#include "retro_pgm2_cards.h"
#include "retro_common.h"

#include "burn.h"
#include "state.h"
#include "drv/pgm2/pgm2.h"

#include <retro_dirent.h>
#include <file/file_path.h>
#include <streams/file_stream.h>
#include <libretro.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <time.h>
#include <errno.h>
#include <vector>

#define ADD_NEW_OPTION 1

extern char g_save_dir[MAX_PATH];

static const INT32 kPgm2CardMaxChoices = 120; // kPgm2CardMaxChoices is constrained to <= RETRO_NUM_CORE_OPTION_VALUES_MAX (128)

static const INT32 PGM2_CARD_SIZE = 0x108;
static const INT32 PGM2_CARD_DATA_SIZE = 0x100;

/** Slots last exposed in core options (may be provisional 4 before BurnDrvInit sets Pgm2MaxCardSlots). */
static int s_pgm2_card_option_slots = 0;
static std::vector<std::string> s_file_paths[4]; // index 0 = first file (choice "1")
static std::string s_opt_key_str[4];
static std::string s_opt_desc_str[4];
static std::string s_opt_info_str[4];
static std::vector<std::string> s_opt_label_storage[4]; // paired: value, label, value, label, ...
static retro_core_option_v2_definition s_opt_def[4];
static char s_last_applied[4][16];
static UINT8 s_pending_card_image[PGM2_CARD_SIZE];
static std::string s_active_file_path[4];

static const struct {
    const char* drv_name;
    INT32 max_slots;
} pgm2_slot_table[] = {
    // ORLEG2
    { "orleg2",           0 },  // overseas (V104)
    { "orleg2_103",       0 },  // overseas
    { "orleg2_101",       0 },  // overseas
    { "orleg2_104jp",     0 },  // Japan
    { "orleg2_103jp",     0 },  // Japan
    { "orleg2_101jp",     0 },  // Japan
    { "orleg2_104cn",     4 },  // China
    { "orleg2_103cn",     4 },  // China
    { "orleg2_101cn",     4 },  // China
    { "orleg2_104hk",     4 },  // Hong Kong
    { "orleg2_103hk",     4 },  // Hong Kong
    { "orleg2_101hk",     4 },  // Hong Kong
    { "orleg2_104tw",     4 },  // Taiwan
    { "orleg2_103tw",     4 },  // Taiwan
    { "orleg2_101tw",     4 },  // Taiwan

    // KOV2NL
    { "kov2nl",           4 },  // overseas (V302)
    { "kov2nl_301",       4 },  // overseas
    { "kov2nl_300",       4 },  // overseas
    { "kov2nl_302jp",     4 },  // Japan
    { "kov2nl_301jp",     4 },  // Japan
    { "kov2nl_300jp",     4 },  // Japan
    { "kov2nl_302cn",     4 },  // China
    { "kov2nl_301cn",     4 },  // China
    { "kov2nl_300cn",     4 },  // China
    { "kov2nl_302hk",     4 },  // Hong Kong
    { "kov2nl_301hk",     4 },  // Hong Kong
    { "kov2nl_300hk",     4 },  // Hong Kong
    { "kov2nl_302tw",     4 },  // Taiwan
    { "kov2nl_301tw",     4 },  // Taiwan
    { "kov2nl_300tw",     4 },  // Taiwan

    // KOV3
    { "kov3",             2 },
    { "kov3_102",         2 },
    { "kov3_101",         2 },
    { "kov3_100",         2 },

    // No card slots
    { "ddpdojt",          0 },
    { "kof98umh",         0 },
};

static INT32 get_pgm2_slot_count(const char* drv_name)
{
    if (!drv_name) return 0;

    for (size_t i = 0; i < sizeof(pgm2_slot_table) / sizeof(pgm2_slot_table[0]); i++) {
        if (strcmp(pgm2_slot_table[i].drv_name, drv_name) == 0) {
            return pgm2_slot_table[i].max_slots;
        }
    }
    return 0;
}

static int iequals_suffix(const char* name, const char* suf)
{
	size_t ln = strlen(name), ls = strlen(suf);
	if (ln < ls) return 0;
	const char* a = name + (ln - ls);
	for (; *suf; ++a, ++suf) {
		char c1 = *a, c2 = *suf;
		if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
		if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
		if (c1 != c2) return 0;
	}
	return 1;
}

static bool pgm2_card_file_ok(const char* name)
{
	return iequals_suffix(name, ".pg2") || iequals_suffix(name, ".bin");
}

/* Core option labels can carry UTF-8, so preserve the original basename for display. */
static std::string label_for_card_file(const char* fullpath)
{
	const char* base = path_basename(fullpath);
	if (!base || !base[0])
		return "card file";
	return std::string(base);
}

static bool get_card_dir_path(char dir[MAX_PATH])
{
	if (!dir || !g_save_dir[0])
		return false;

	snprintf(dir, MAX_PATH, "%s%cfbneo%cpgm2_memcards", g_save_dir, PATH_DEFAULT_SLASH_C(), PATH_DEFAULT_SLASH_C());
	path_mkdir(dir);
	return true;
}

static bool get_default_slot_file_path(int slot, char path[MAX_PATH])
{
	const char* drvname = BurnDrvGetTextA(DRV_NAME);
	char dir[MAX_PATH];
	if (!path || slot < 0 || slot >= 4 || !drvname || !drvname[0] || !get_card_dir_path(dir))
		return false;

	snprintf(path, MAX_PATH, "%s%c%s_p%d_default.pg2", dir, PATH_DEFAULT_SLASH_C(), drvname, slot + 1);
	return true;
}

static bool is_default_slot_file(const char* name, const char* drvname, int slot)
{
	char default_name[160];
	if (!name || !drvname || !drvname[0] || slot < 0 || slot >= 4)
		return false;

	snprintf(default_name, sizeof(default_name), "%s_p%d_default.pg2", drvname, slot + 1);
	return strcmp(name, default_name) == 0;
}

static void clear_slot_option(int slot)
{
	memset(&s_opt_def[slot], 0, sizeof(s_opt_def[slot]));
	s_opt_label_storage[slot].clear();
}

static void clear_slot_state(int slot)
{
	clear_slot_option(slot);
	s_file_paths[slot].clear();
}

static void rebuild_scan();

static bool read_raw_card_file(const char* path, UINT8* dest, size_t dest_len)
{
	RFILE* fp = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, 0);
	if (!fp) return false;
	int64_t sz = filestream_get_size(fp);
	if (sz < 0) {
		filestream_close(fp);
		return false;
	}
	memset(dest, 0xff, dest_len);
	if (sz == PGM2_CARD_SIZE) {
		if (filestream_read(fp, dest, PGM2_CARD_SIZE) != PGM2_CARD_SIZE) {
			filestream_close(fp);
			return false;
		}
	} else if (sz == PGM2_CARD_DATA_SIZE) {
		if (filestream_read(fp, dest, PGM2_CARD_DATA_SIZE) != PGM2_CARD_DATA_SIZE) {
			filestream_close(fp);
			return false;
		}
		if (dest_len >= PGM2_CARD_SIZE) {
			memset(dest + PGM2_CARD_DATA_SIZE, 0xff, 4);
			dest[0x104] = 0x07;
			dest[0x105] = 0xff;
			dest[0x106] = 0xff;
			dest[0x107] = 0xff;
		}
	} else {
		filestream_close(fp);
		return false;
	}
	filestream_close(fp);
	return true;
}

static bool write_raw_card_file(const char* path, const UINT8* src, size_t src_len)
{
	if (!path || !path[0] || !src || src_len < PGM2_CARD_SIZE)
		return false;

	size_t out_len = PGM2_CARD_SIZE;
	RFILE* rf = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, 0);
	if (rf) {
		int64_t sz = filestream_get_size(rf);
		if (sz == PGM2_CARD_DATA_SIZE)
			out_len = PGM2_CARD_DATA_SIZE;
		else if (sz == PGM2_CARD_SIZE)
			out_len = PGM2_CARD_SIZE;
		filestream_close(rf);
	} else if (iequals_suffix(path, ".bin")) {
		out_len = PGM2_CARD_DATA_SIZE;
	}

	RFILE* fp = filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE, 0);
	if (!fp)
		return false;

	bool ok = (filestream_write(fp, src, (int64_t)out_len) == (int64_t)out_len);
	filestream_close(fp);
	return ok;
}

static bool build_builtin_card_image(UINT8* dest, size_t dest_len)
{
	if (!dest || dest_len < PGM2_CARD_SIZE)
		return false;
	if (pgm2GetCardRomTemplate(dest, (INT32)dest_len) >= PGM2_CARD_SIZE)
		return true;

	memset(dest, 0xff, dest_len);
	dest[0x104] = 0x07;
	return true;
}

static bool load_or_create_default_slot_card(int slot)
{
	char path[MAX_PATH];
	if (!get_default_slot_file_path(slot, path))
		return false;

	if (filestream_exists(path)) {
		if (!read_raw_card_file(path, s_pending_card_image, sizeof(s_pending_card_image))) {
			log_cb(RETRO_LOG_WARN,
				"[FBNeo PGM2 cards] slot P%d: default card file \"%s\" is invalid; recreating from ROM template\n",
				slot + 1, path);
		} else {
			s_active_file_path[slot] = path;
			return true;
		}
	}

	if (!build_builtin_card_image(s_pending_card_image, sizeof(s_pending_card_image)))
		return false;
	if (!write_raw_card_file(path, s_pending_card_image, sizeof(s_pending_card_image)))
		return false;

	s_active_file_path[slot] = path;
	log_cb(RETRO_LOG_INFO,
		"[FBNeo PGM2 cards] slot P%d: created default card file \"%s\"\n",
		slot + 1, path);
	return true;
}

#if ADD_NEW_OPTION
static bool create_timestamped_slot_card(int slot, std::string& out_path)
{
	char dir[MAX_PATH];
	char path[MAX_PATH];
	char stamp[32];
	const char* drvname = BurnDrvGetTextA(DRV_NAME);
	time_t now;
	struct tm tm_now;

	if (slot < 0 || slot >= 4 || !drvname || !drvname[0] || !get_card_dir_path(dir))
		return false;
	if (!build_builtin_card_image(s_pending_card_image, sizeof(s_pending_card_image)))
		return false;

	now = time(NULL);
	if (now == (time_t)-1)
		return false;
#ifdef _WIN32
	if (localtime_s(&tm_now, &now) != 0) {
        return false;
    }
#else
	if (!localtime_r(&now, &tm_now))
		return false;
#endif
	if (strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm_now) == 0)
		return false;

	snprintf(path, sizeof(path), "%s%c%s_p%d_%s.pg2", dir, PATH_DEFAULT_SLASH_C(), drvname, slot + 1, stamp);
	for (int attempt = 0; filestream_exists(path); attempt++) {
		if (attempt >= 99)
			return false;
		snprintf(path, sizeof(path), "%s%c%s_p%d_%s_%02d.pg2", dir, PATH_DEFAULT_SLASH_C(), drvname, slot + 1, stamp, attempt + 1);
	}

	if (!write_raw_card_file(path, s_pending_card_image, sizeof(s_pending_card_image)))
		return false;

	out_path.assign(path);
	return true;
}

static int find_slot_file_choice(int slot, const char* path)
{
	if (slot < 0 || slot >= 4 || !path || !path[0])
		return -1;

	for (size_t i = 0; i < s_file_paths[slot].size(); i++) {
		if (s_file_paths[slot][i] == path)
			return (int)i + 1;
	}

	return -1;
}
#endif

static INT32 __cdecl pgm2_card_noop_callback(struct BurnArea*)
{
	return 0;
}

static INT32 __cdecl pgm2_card_insert_from_buffer(struct BurnArea* pba)
{
	if (!pba || !pba->Data || pba->nLen < sizeof(s_pending_card_image))
		return 1;

	memcpy(pba->Data, s_pending_card_image, sizeof(s_pending_card_image));
	return 0;
}

static void reinsert_slot_with_pending_image(int slot)
{
	INT32 nMinVersion = 0;
	INT32 (__cdecl *prev_burn_acb)(struct BurnArea*) = BurnAcb;

	Pgm2ActiveCardSlot = slot;

	BurnAcb = pgm2_card_noop_callback;
	BurnAreaScan(ACB_READ | ACB_MEMCARD | ACB_MEMCARD_ACTION, &nMinVersion);

	BurnAcb = pgm2_card_insert_from_buffer;
	BurnAreaScan(ACB_WRITE | ACB_MEMCARD | ACB_MEMCARD_ACTION, &nMinVersion);

	BurnAcb = prev_burn_acb;
}

static void eject_slot(int slot) {

    if (slot < 0 || slot >= 4)
		return;

    Pgm2ActiveCardSlot = slot;

    if (Pgm2Cards[slot]) {
        memset(Pgm2Cards[slot], 0xFF, PGM2_CARD_SIZE);
    }

    if (!s_active_file_path[slot].empty()) {
        s_active_file_path[slot].clear();
    }

    INT32 nMinVersion = 0;
    BurnAreaScan(ACB_READ | ACB_MEMCARD | ACB_MEMCARD_ACTION, &nMinVersion);
}

static void save_active_slot_file(int slot)
{
	if (slot < 0 || slot >= 4)
		return;
	if (s_active_file_path[slot].empty() || !Pgm2Cards[slot])
		return;

	if (write_raw_card_file(s_active_file_path[slot].c_str(), Pgm2Cards[slot], PGM2_CARD_SIZE)) {
		log_cb(RETRO_LOG_INFO, "[FBNeo PGM2 cards] slot P%d: saved card file \"%s\"\n",
			slot + 1, s_active_file_path[slot].c_str());
	} else {
		log_cb(RETRO_LOG_ERROR, "[FBNeo PGM2 cards] slot P%d: failed to save card file \"%s\"\n",
			slot + 1, s_active_file_path[slot].c_str());
	}
}

static void scan_slot_files(int slot, const char* drvname, char dir[MAX_PATH])
{
	char prefix[128];
	snprintf(prefix, sizeof(prefix), "%s_p%d_", drvname, slot + 1);

	struct RDIR* d = retro_opendir_include_hidden(dir, true);
	if (!d || retro_dirent_error(d)) {
		log_cb(RETRO_LOG_INFO,
			"[FBNeo PGM2 cards] slot P%d: cannot read directory \"%s\" (prefix \"%s\")\n",
			slot + 1, dir, prefix);
		if (d) retro_closedir(d);
		return;
	}

	std::vector<std::string> names;
	while (retro_readdir(d)) {
		const char* name = retro_dirent_get_name(d);
		if (!name || retro_dirent_is_dir(d, NULL)) continue;
		if (strncmp(name, prefix, strlen(prefix)) != 0) continue;
		if (!pgm2_card_file_ok(name)) continue;
		if (is_default_slot_file(name, drvname, slot)) continue;
		names.push_back(name);
	}
	retro_closedir(d);

	std::sort(names.begin(), names.end());
	if ((int)names.size() > kPgm2CardMaxChoices)
		names.resize(kPgm2CardMaxChoices);

	for (size_t i = 0; i < names.size(); i++) {
		char fp[MAX_PATH];
		snprintf(fp, sizeof(fp), "%s%c%s", dir, PATH_DEFAULT_SLASH_C(), names[i].c_str());
		s_file_paths[slot].push_back(fp);
	}

	log_cb(RETRO_LOG_INFO,
		"[FBNeo PGM2 cards] slot P%d: prefix \"%s\" -> %u file(s) in \"%s\"\n",
		slot + 1, prefix, (unsigned)names.size(), dir);
	for (size_t i = 0; i < names.size(); i++)
		log_cb(RETRO_LOG_INFO, "[FBNeo PGM2 cards]   [%u] %s\n", (unsigned)(i + 1), names[i].c_str());
}


static void add_fixed_option(std::vector<std::string>& L, const char* value, const char* label) {
    L.push_back(value);
    L.push_back(label);
}

static void add_file_option(std::vector<std::string>& L, const char* path, std::string label) {
    L.push_back(path);
    L.push_back(label);
}

static void build_slot_option(int slot)
{
	clear_slot_option(slot);

	char key[48];
	/* Key changed from fbneo-pgm2-pN-card so stale/broken fbneo.opt entries cannot hide values. */
	snprintf(key, sizeof(key), "fbneo-pgm2-ic-p%d", slot + 1);
	s_opt_key_str[slot].assign(key);

	std::vector<std::string>& L = s_opt_label_storage[slot];
	L.clear();

	// 4 fixed options
	add_fixed_option(L, "empty",        RETRO_PGM2_EMPTY_SLOT);
	add_fixed_option(L, "default",      RETRO_PGM2_DEFAULT_CARD);
	add_fixed_option(L, "temporary",    RETRO_PGM2_TEMPORARY_CARD);
#if ADD_NEW_OPTION
	add_fixed_option(L, "new",          RETRO_PGM2_NEW_CARD);
#endif

	const size_t nfiles = s_file_paths[slot].size();
	char idx[12];
	for (size_t i = 0; i < nfiles; i++) {
		snprintf(idx, sizeof(idx), "%u", (unsigned)(i + 1));
		std::string label = label_for_card_file(s_file_paths[slot][i].c_str());
		add_file_option(L, idx, label);
	}

	char slot_ch = (char)('1' + slot);
	/* Keep desc 7-bit ASCII only; some frontends reject or mangle UTF-8 punctuation in SET_CORE_OPTIONS_V2. */
	char buf[96];
	snprintf(buf, sizeof(buf), RETRO_PGM2_MEMORY_CARD_SLOT_DESC, slot_ch, (unsigned)nfiles);
	s_opt_desc_str[slot].assign(buf);

	const char* drv = BurnDrvGetTextA(DRV_NAME);
	if (!drv) drv = "";
	s_opt_info_str[slot] = RETRO_PGM2_MEMORY_CARD_SLOT_INFO_1;
	s_opt_info_str[slot] += drv;
	s_opt_info_str[slot] += RETRO_PGM2_MEMORY_CARD_SLOT_INFO_2;

	retro_core_option_v2_definition& def = s_opt_def[slot];
	def.key = s_opt_key_str[slot].c_str();
	def.desc = s_opt_desc_str[slot].c_str();
	def.info = s_opt_info_str[slot].c_str();
	def.category_key = "pgm2_memory_card";

	int nvals = L.size() / 2;
	for (int i = 0; i < nvals; i++) {
		def.values[i].value = L[i * 2].c_str();
		def.values[i].label = L[i * 2 + 1].c_str();
	}

	def.values[nvals].value = NULL;
	def.values[nvals].label = NULL;
	/* default_value must match a values[].value string (same pointer is safest). */
	def.default_value = def.values[0].value;

	log_cb(RETRO_LOG_INFO, "[FBNeo PGM2 cards] register core option \"%s\" (%d values)\n", def.key, nvals);
}

void retro_pgm2_cards_reset()
{
	s_pgm2_card_option_slots = 0;
	for (int i = 0; i < 4; i++) {
		clear_slot_state(i);
		s_opt_key_str[i].clear();
		s_opt_desc_str[i].clear();
		s_opt_info_str[i].clear();
		s_active_file_path[i].clear();
		memset(s_last_applied[i], 0, sizeof(s_last_applied[i]));
	}
}

void retro_pgm2_cards_push_options(std::vector<const retro_core_option_v2_definition*>& vars_systems)
{
	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) != HARDWARE_IGS_PGM2)
		return;

	rebuild_scan();

	for (int s = 0; s < s_pgm2_card_option_slots && s < 4; s++)
		vars_systems.push_back(&s_opt_def[s]);
}

static void rebuild_scan()
{
	s_pgm2_card_option_slots = 0;
	for (int i = 0; i < 4; i++){
		clear_slot_state(i);
		memset(s_last_applied[i], 0, sizeof(s_last_applied[i]));
	}

	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) != HARDWARE_IGS_PGM2)
		return;

	const char* drvname = BurnDrvGetTextA(DRV_NAME);
	if (!drvname || !drvname[0])
		return;

	// Get slot_count from table, instead of "Pgm2MaxCardSlots", which comes from BurnDrvInit()
	int slots_eff = get_pgm2_slot_count(drvname);
	if (slots_eff <= 0)
		return;

	char dir[MAX_PATH];
	if (!get_card_dir_path(dir))
		return;
	path_mkdir(dir);

	log_cb(RETRO_LOG_INFO,
		"[FBNeo PGM2 cards] scan: save_dir=\"%s\" card_dir=\"%s\" drvname=\"%s\" slots=%d\n",
		g_save_dir, dir, drvname, slots_eff);

	for (int s = 0; s < slots_eff && s < 4; s++) {
		scan_slot_files(s, drvname, dir);
		build_slot_option(s);
	}

	s_pgm2_card_option_slots = slots_eff;
}

void retro_pgm2_cards_refresh_environment()
{
	for (int i = 0; i < 4; i++)
		memset(s_last_applied[i], 0, sizeof(s_last_applied[i]));
	retro_pgm2_cards_apply_variables();
}

static void apply_one_slot(int slot)
{
	if (slot < 0 || slot >= 4 || !Pgm2Cards[slot])
		return;

	struct retro_variable var = {0};
	char key[48];
	snprintf(key, sizeof(key), "fbneo-pgm2-ic-p%d", slot + 1);
	var.key = key;
	if (!environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || !var.value || var.value[0] == '\0'){
		memset(s_last_applied[slot], 0, sizeof(s_last_applied[slot]));
		return;
	}
	if (strcmp(s_last_applied[slot], var.value) == 0)
		return;
	strncpy(s_last_applied[slot], var.value, sizeof(s_last_applied[slot]) - 1);
	s_last_applied[slot][sizeof(s_last_applied[slot]) - 1] = '\0';

	save_active_slot_file(slot);

	//Empty Slots
	if (strcmp(var.value, "empty") == 0) {
		eject_slot(slot);
		log_cb(RETRO_LOG_WARN, "[FBNeo PGM2 cards] slot P%d: Use Empty Slot; Eject Card File.\n", slot + 1);
		strncpy(s_last_applied[slot], "empty", sizeof(s_last_applied[slot]) - 1);
		s_last_applied[slot][sizeof(s_last_applied[slot]) - 1] = '\0';
		return;
	}

	// Use Default Memory Card File ,end with '_defalut', Only one exists. If Failed ,fall back to Temporary Card.
	if (strcmp(var.value, "default") == 0) {
		if (!load_or_create_default_slot_card(slot)) {
			if (!build_builtin_card_image(s_pending_card_image, sizeof(s_pending_card_image))) {
				log_cb(RETRO_LOG_ERROR, "[FBNeo PGM2 cards] slot P%d: failed to build default ROM card template\n", slot + 1);
				memset(s_last_applied[slot], 0, sizeof(s_last_applied[slot]));
				return;
			}
			s_active_file_path[slot].clear();
			log_cb(RETRO_LOG_WARN,
				"[FBNeo PGM2 cards] slot P%d: failed to load/create default card file; using temporary ROM template\n",
				slot + 1);
		}

		reinsert_slot_with_pending_image(slot);
		if (!s_active_file_path[slot].empty()) {
			log_cb(RETRO_LOG_INFO, "[FBNeo PGM2 cards] slot P%d: using default card file \"%s\"\n",
				slot + 1, s_active_file_path[slot].c_str());
		} else {
			log_cb(RETRO_LOG_INFO, "[FBNeo PGM2 cards] slot P%d: using temporary default ROM template\n",
				slot + 1);
		}
		return;
	}

	// Use IN-Memory Temporary Card, No File. Expires on Exit Game.
	if (strcmp(var.value, "temporary") == 0) {
		if (!build_builtin_card_image(s_pending_card_image, sizeof(s_pending_card_image))) {
			log_cb(RETRO_LOG_ERROR, "[FBNeo PGM2 cards] slot P%d: failed to build built-in ROM card template\n", slot + 1);
			memset(s_last_applied[slot], 0, sizeof(s_last_applied[slot]));
			return;
		}

		reinsert_slot_with_pending_image(slot);
		s_active_file_path[slot].clear();
		log_cb(RETRO_LOG_INFO, "[FBNeo PGM2 cards] slot P%d: using built-in ROM template (option value \"%s\")\n",
			slot + 1, var.value);
		return;
	}

#if ADD_NEW_OPTION
	// Create New Memory Card File, end with '_timestamped'. Multiple exist.
	if (strcmp(var.value, "new") == 0) {
		std::string new_path;
		if (!create_timestamped_slot_card(slot, new_path)) {
			log_cb(RETRO_LOG_ERROR, "[FBNeo PGM2 cards] slot P%d: failed to create timestamped card file\n", slot + 1);
			memset(s_last_applied[slot], 0, sizeof(s_last_applied[slot]));
			return;
		}

		reinsert_slot_with_pending_image(slot);
		s_active_file_path[slot] = new_path;
		log_cb(RETRO_LOG_INFO, "[FBNeo PGM2 cards] slot P%d: created timestamped card file \"%s\"\n",
			slot + 1, new_path.c_str());

		/**
			Simply calling rebuild_scan() cannot make the frontend indicate the correct option.
			Repeatedly calling set_environment() can lead to memory write leaks and DIP function confusion, requiring exiting and reloading the game to resolve.
			https://github.com/libretro/FBNeo/commit/4cc9971b20285e52ac7b990daf61931a7a17f812
			This option hided by ADD_NEW_OPTION
		*/
		set_environment();

		int new_choice = find_slot_file_choice(slot, new_path.c_str());
		if (new_choice > 0) {
			struct retro_variable set_var = {0};
			char key_apply[48];
			char choice_buf[12];
			snprintf(key_apply, sizeof(key_apply), "fbneo-pgm2-ic-p%d", slot + 1);
			snprintf(choice_buf, sizeof(choice_buf), "%d", new_choice);
			set_var.key = key_apply;
			set_var.value = choice_buf;
			environ_cb(RETRO_ENVIRONMENT_SET_VARIABLE, &set_var);
			strncpy(s_last_applied[slot], choice_buf, sizeof(s_last_applied[slot]) - 1);
			s_last_applied[slot][sizeof(s_last_applied[slot]) - 1] = '\0';
		} else {
			memset(s_last_applied[slot], 0, sizeof(s_last_applied[slot]));
		}
		return;
	}
#endif

	//Choose memory card from 'g_save_dir'/fbneo/pgm2_memcards/. Default Card excluded.
	int choice = atoi(var.value);
	int fi = choice - 1;
	if (fi < 0 || fi >= (int)s_file_paths[slot].size()) {
		log_cb(RETRO_LOG_WARN,
			"[FBNeo PGM2 cards] slot P%d: invalid card index %d (have %u files); ignoring\n",
			slot + 1, choice, (unsigned)s_file_paths[slot].size());
		memset(s_last_applied[slot], 0, sizeof(s_last_applied[slot]));
		return;
	}

	if (read_raw_card_file(s_file_paths[slot][fi].c_str(), s_pending_card_image, sizeof(s_pending_card_image))) {
		reinsert_slot_with_pending_image(slot);
		s_active_file_path[slot] = s_file_paths[slot][fi];
		log_cb(RETRO_LOG_INFO, "[FBNeo PGM2 cards] slot P%d: loaded card file \"%s\"\n",
			slot + 1, s_file_paths[slot][fi].c_str());
	} else {
		log_cb(RETRO_LOG_ERROR, "[FBNeo PGM2 cards] slot P%d: failed to read card file \"%s\" (expect 256 or 264 bytes)\n",
			slot + 1, s_file_paths[slot][fi].c_str());
		memset(s_last_applied[slot], 0, sizeof(s_last_applied[slot]));
	}
}

void retro_pgm2_cards_save_files()
{
	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) != HARDWARE_IGS_PGM2)
		return;

	for (int s = 0; s < 4; s++)
		save_active_slot_file(s);
}

void retro_pgm2_cards_apply_variables()
{
	if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) != HARDWARE_IGS_PGM2)
		return;

	INT32 n = get_pgm2_slot_count(BurnDrvGetTextA(DRV_NAME));
	if (n <= 0) n = s_pgm2_card_option_slots;

	for (int s = 0; s < n && s < 4; s++)
		apply_one_slot(s);
}

#endif
