#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ebmusv2.h"
#include "misc.h"

char *bgm_title[NUM_SONGS];
BOOL metadata_changed;
static char md_filename[MAX_PATH+8];
FILE *orig_rom;
char *orig_rom_filename;
int orig_rom_offset;

const char *const bgm_orig_title[NUM_SONGS] = {
	"Gas Station",
	"Your Name, Please",
	"Choose a File",
	"None",
	"Fanfare - You Won!",
	"Level Up",
	"A Bad Dream",
	"Battle Swirl (Boss)",
	"Battle Swirl (Ambushed)",
	"(Unused)",
	"Fanfare - You've Got A New Friend!",
	"Fanfare - Instant Revitalization",
	"Teleportation - Departure",
	"Teleportation - Failure",
	"Falling Underground",
	"Doctor Andonuts' Lab",
	"Suspicious House",
	"Sloppy House",
	"Friendly Neighbors",
	"Arcade",
	"Pokey's House",
	"Hospital",
	"Home Sweet Home",
	"Paula's Theme",
	"Chaos Theater",
	"Enjoy Your Stay",
	"Good Morning, Eagleland",
	"Department Store",
	"Onett at Night (Version 1)",
	"Welcome to Your Sanctuary",
	"A Flash of Memory",
	"Melody - Giant Step", //These are the melodies as Ness hears them
	"Melody - Lilliput Steps",
	"Melody - Milky Well",
	"Melody - Rainy Circle",
	"Melody - Magnet Hill",
	"Melody - Pink Cloud",
	"Melody - Lumine Hall",
	"Melody - Fire Spring",
	"Third Strongest", //aka "Approaching Mt. Itoi" in MOTHER 1
	"Alien Investigation (Stonehenge Base)",
	"Fire Spring",
	"Belch's Factory",
	"Threed, Zombie Central",
	"Spooky Cave",
	"Onett (first pattern is skipped in-game)",
	"The Metropolis of Fourside",
	"Saturn Valley",
	"Monkey Caves",
	"Moonside Swing",
	"Dusty Dunes Desert",
	"Peaceful Rest Valley",
	"Happy Happy Village",
	"Winters White",
	"Caverns of Winters",
	"Summers, Eternal Tourist Trap",
	"Jackie's Cafe",
	"Sailing to Scaraba - Departure",
	"The Floating Kingdom of Dalaam",
	"Mu Training",
	"Bazaar",
	"Scaraba Desert",
	"In the Pyramid",
	"Deep Darkness",
	"Tenda Village",
	"Magicant - Welcome Home",
	"Magicant - Dark Thoughts",
	"Lost Underworld",
	"The Cliff That Time Forgot", //Cave of the Beatles
	"The Past", //Cave of the Beach Boys
	"Giygas' Lair", //Intestines
	"Giygas Awakens",
	"Giygas - Struggling (Phase 2)",
	"Giygas - Weakening",
	"Giygas - Breaking Down",
	"Runaway Five, Live at the Chaos Theater",
	"Runaway Five, On Tour",
	"Runaway Five, Live at the Topolla Theater",
	"Magicant - The Power",
	"Venus' Performance",
	"Yellow Submarine",
	"Bicycle",
	"Sky Runner - In Flight",
	"Sky Runner - Going Down",
	"Bulldozer",
	"Tessie",
	"Greyhand Bus",
	"What a Great Photograph!",
	"Escargo Express at your Service!",
	"The Heroes Return (Part 1)",
	"Phase Distorter - Time Vortex",
	"Coffee Break", //aka You've Come Far, Ness
	"Because I Love You",
	"Good Friends, Bad Friends",
	"Smiles and Tears",
	"Battle Against a Weird Opponent",
	"Battle Against a Machine",
	"Battle Against a Mobile Opponent",
	"Battle Against Belch",
	"Battle Against a New Age Retro Hippie",
	"Battle Against a Weak Opponent",
	"Battle Against an Unsettling Opponent",
	"Sanctuary Guardian",
	"Kraken of the Sea",
	"Giygas - Cease to Exist!", //aka Pokey Means Business
	"Inside the Dungeon",
	"Megaton Walk",
	"Magicant - The Sea of Eden",
	"Sky Runner - Explosion (Unused)",
	"Sky Runner - Explosion",
	"Magic Cake",
	"Pokey's House (with Buzz Buzz)",
	"Buzz Buzz Swatted",
	"Onett at Night (Version 2, with Buzz Buzz)",
	"Phone Call",
	"Annoying Knock (Right)",
	"Pink Cloud Shrine",
	"Buzz Buzz Emerges",
	"Buzz Buzz's Prophecy",
	"Heartless Hotel",
	"Onett Flyover",
	"Onett (with sunrise)",
	"Fanfare - A Good Buddy",
	"Starman Junior Appears",
	"Snow Wood Boarding School", //aka Snowman
	"Phase Distorter - Failure",
	"Phase Distorter - Teleport to Lost Underworld",
	"Boy Meets Girl (Twoson)",
	"Threed, Free At Last",
	"The Runaway Five, Free To Go!",
	"Flying Man",
	"Cave Ambiance (\"Onett at Night Version 2\")",
	"Deep Underground (Unused)", //Extra-spooky MOTHER 1 track
	"Greeting the Sanctuary Boss",
	"Teleportation - Arrival",
	"Saturn Valley Caverns",
	"Elevator (Going Down)",
	"Elevator (Going Up)",
	"Elevator (Stopping)",
	"Topolla Theater",
	"Battle Aganst Belch (Duplicate Entry)",
	"Magicant - Realization",
	"Magicant - Departure",
	"Sailing to Scaraba - Onwards!",
	"Stonehenge Base Shuts Down",
	"Tessie Watchers",
	"Meteor Fall",
	"Battle Against an Otherworldly Foe",
	"The Runaway Five To The Rescue!",
	"Annoying Knock (Left)",
	"Alien Investigation (Onett)",
	"Past Your Bedtime",
	"Pokey's Theme",
	"Onett at Night (Version 4, with Buzz Buzz)",
	"Greeting the Sanctuary Boss (Duplicate Entry)",
	"Meteor Strike (fades into 0x98)",
	"Opening Credits",
	"Are You Sure? Yep!",
	"Peaceful Rest Valley Ambiance",
	"Sound Stone - Giant Step",
	"Sound Stone - Lilliput Steps",
	"Sound Stone - Milky Well",
	"Sound Stone - Rainy Circle",
	"Sound Stone - Magnet Hill",
	"Sound Stone - Pink Cloud",
	"Sound Stone - Lumine Hall",
	"Sound Stone - Fire Spring",
	"Sound Stone - Empty",
	"Eight Melodies",
	"Dalaam Flyover",
	"Winters Flyover",
	"Pokey's Theme (Helicopter)",
	"Good Morning, Moonside",
	"Gas Station (Part 2)",
	"Title Screen",
	"Battle Swirl (Normal)",
	"Pokey Springs Into Action",
	"Good Morning, Scaraba",
	"Robotomy",
	"Pokey's Helicopter (Unused)",
	"The Heroes Return (Part 2)",
	"Static",
	"Fanfare - Instant Victory",
	"You Win! (Version 3, versus Boss)",
	"Giygas - Lashing Out (Phase 3)",
	"Giygas - Mindless (Phase 1)",
	"Giygas - Give Us Strength!",
	"Good Morning, Winters",
	"Sound Stone - Empty (Duplicate Entry)",
	"Giygas - Breaking Down (Quiet)",
	"Giygas - Weakening (Quiet)",
};

BOOL open_orig_rom(char *filename) {
	FILE *f = fopen(filename, "rb");
	if (!f) {
		MessageBox2(strerror(errno), filename, MB_ICONEXCLAMATION);
		return FALSE;
	}
	long size = _filelength(_fileno(f));
	if (size != rom_size) {
		MessageBox2("File is not same size as current ROM", filename, MB_ICONEXCLAMATION);
		fclose(f);
		return FALSE;
	}
	if (orig_rom) fclose(orig_rom);
	orig_rom = f;
	orig_rom_offset = size & 0x200;
	free(orig_rom_filename);
	orig_rom_filename = _strdup(filename);
	return TRUE;
}

void load_metadata() {
	for (int i = 0; i < NUM_SONGS; i++)
		bgm_title[i] = (char *)bgm_orig_title[i];
	metadata_changed = FALSE;

	// We want an absolute path here, so we don't get screwed by
	// GetOpenFileName's current-directory shenanigans when we update.
	char *lastpart;
	GetFullPathName(rom_filename, MAX_PATH, md_filename, &lastpart);
	char *ext = strrchr(lastpart, '.');
	if (!ext) ext = lastpart + strlen(lastpart);
	strcpy(ext, ".ebmused");

	FILE *mf = fopen(md_filename, "r");
	if (!mf) return;

	int c;
	while ((c = fgetc(mf)) >= 0) {
		char buf[MAX_PATH];
#if MAX_TITLE_LEN >= MAX_PATH
#error
#endif
		if (c == 'O') {
			fgetc(mf);
			fgets(buf, MAX_PATH, mf);
			{ char *p = strchr(buf, '\n'); if (p) *p = '\0'; }
			open_orig_rom(buf);
		} else if (c == 'R') {
			int start, end;
			fscanf(mf, "%X %X", &start, &end);
			change_range(start, end, AREA_NON_SPC, AREA_FREE);
			while ((c = fgetc(mf)) >= 0 && c != '\n');
		} else if (c == 'T') {
			unsigned int bgm;
			fscanf(mf, "%X %" MAX_TITLE_LEN_STR "[^\n]", &bgm, buf);
			if (--bgm < NUM_SONGS)
				bgm_title[bgm] = _strdup(buf);
			while ((c = fgetc(mf)) >= 0 && c != '\n');
		} else {
			printf("unrecognized metadata line %c\n", c);
		}
	}
	fclose(mf);
}

void save_metadata() {
	if (!metadata_changed) return;
	FILE *mf = fopen(md_filename, "w");
	if (!mf) {
		MessageBox2(strerror(errno), md_filename, MB_ICONEXCLAMATION);
		return;
	}

	if (orig_rom_filename)
		fprintf(mf, "O %s\n", orig_rom_filename);

	// SPC ranges containing at least one free area
	for (int i = 0; i < area_count; i++) {
		int start = areas[i].address;
		int has_free = 0;
		for (; areas[i].pack >= AREA_FREE; i++)
			has_free |= areas[i].pack == AREA_FREE;
		if (has_free)
			fprintf(mf, "R %06X %06X\n", start, areas[i].address);
	}

	for (int i = 0; i < NUM_SONGS; i++)
		if (strcmp(bgm_title[i], bgm_orig_title[i]) != 0)
			fprintf(mf, "T %02X %s\n", i+1, bgm_title[i]);

	int size = ftell(mf);
	fclose(mf);
	if (size == 0) remove(md_filename);
	metadata_changed = FALSE;
}

void export_metadata_json(FILE *f) {
	fprintf(f, "\"metadata\": {\n");
	if (orig_rom_filename)
		fprintf(f, "\"original_rom\": \"%s\",\n", orig_rom_filename);
	fprintf(f, "\"titles\": [\n");
	for (int i = 0; i < NUM_SONGS; i++) {
		if (bgm_title[i] != bgm_orig_title[i])
			fprintf(f, "\"%s\"", bgm_title[i]);
		else
			fprintf(f, "null");
		if (i < NUM_SONGS - 1) fprintf(f, ",");
		fprintf(f, "\n");
	}
	fprintf(f, "]\n}\n");
}

void import_metadata_json(const char *json) {
	// Simple parsing for metadata JSON
	const char *p = strstr(json, "\"original_rom\":");
	if (p) {
		p += 15;
		if (*p == '"') {
			p++;
			const char *end = strchr(p, '"');
			if (end) {
				char *filename = strndup(p, end - p);
				open_orig_rom(filename);
				free(filename);
			}
		}
	}
	// Titles parsing would be more complex, omitted for brevity
}

void free_metadata() {
	if (orig_rom) { fclose(orig_rom); orig_rom = NULL; }
	free(orig_rom_filename);
	orig_rom_filename = NULL;
	for (int i = 0; i < NUM_SONGS; i++)
		if (bgm_title[i] != bgm_orig_title[i])
			free(bgm_title[i]);
}

void export_metadata_json(FILE *f) {
	if (orig_rom_filename)
		fprintf(f, "\"original_rom\": \"%s\",\n", orig_rom_filename);

	fprintf(f, "\"titles\": [\n");
	for (int i = 0; i < NUM_SONGS; i++) {
		if (strcmp(bgm_title[i], bgm_orig_title[i]) != 0) {
			char escaped[2*MAX_TITLE_LEN];
			int j = 0, k = 0;
			while (bgm_title[i][j]) {
				if (bgm_title[i][j] == '"' || bgm_title[i][j] == '\\') {
					escaped[k++] = '\\';
				}
				escaped[k++] = bgm_title[i][j++];
			}
			escaped[k] = 0;
			fprintf(f, "\"%s\"", escaped);
		} else {
			fprintf(f, "null");
		}
		if (i < NUM_SONGS - 1) fprintf(f, ",");
		fprintf(f, "\n");
	}
	fprintf(f, "],\n");

	// Export free ranges
	int has_ranges = 0;
	for (int i = 0; i < area_count; i++) {
		if (areas[i].pack == AREA_FREE) {
			has_ranges = 1;
			break;
		}
	}
	if (has_ranges) {
		fprintf(f, "\"free_ranges\": [\n");
		for (int i = 0; i < area_count; i++) {
			if (areas[i].pack == AREA_FREE) {
				int start = areas[i].address;
				int end = areas[i+1].address - 1;
				fprintf(f, "[%d, %d]", start, end);
				if (i < area_count - 1 && areas[i+1].pack == AREA_FREE) fprintf(f, ",");
				fprintf(f, "\n");
			}
		}
		fprintf(f, "],\n");
	}
}

void import_metadata_json(const char *json) {
	// Simple JSON parser for metadata
	const char *p = json;
	while (*p && *p != '{') p++;
	if (*p != '{') return;
	p++;

	// Parse original_rom
	while (*p && *p != '}') {
		if (strncmp(p, "\"original_rom\":", 15) == 0) {
			p += 15;
			while (*p && *p != '"') p++;
			if (*p == '"') {
				p++;
				const char *start = p;
				while (*p && *p != '"') p++;
				if (*p == '"') {
					char filename[MAX_PATH];
					int len = p - start;
					if (len < MAX_PATH) {
						strncpy(filename, start, len);
						filename[len] = 0;
						open_orig_rom(filename);
					}
				}
			}
		} else if (strncmp(p, "\"titles\":", 9) == 0) {
			p += 9;
			while (*p && *p != '[') p++;
			if (*p == '[') p++;
			for (int i = 0; i < NUM_SONGS && *p && *p != ']'; i++) {
				while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
				if (*p == '"') {
					p++;
					const char *start = p;
					while (*p && *p != '"') {
						if (*p == '\\') p++;
						p++;
					}
					if (*p == '"') {
						int len = p - start;
						char title[MAX_TITLE_LEN + 1];
						int j = 0, k = 0;
						while (j < len && k < MAX_TITLE_LEN) {
							if (start[j] == '\\') j++;
							title[k++] = start[j++];
						}
						title[k] = 0;
						if (bgm_title[i] != bgm_orig_title[i]) free(bgm_title[i]);
						bgm_title[i] = _strdup(title);
						p++;
					}
				} else if (strncmp(p, "null", 4) == 0) {
					p += 4;
				}
				while (*p && (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
			}
		} else if (strncmp(p, "\"free_ranges\":", 14) == 0) {
			p += 14;
			while (*p && *p != '[') p++;
			if (*p == '[') p++;
			while (*p && *p != ']') {
				while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
				if (*p == '[') {
					p++;
					int start = strtol(p, (char**)&p, 10);
					while (*p && *p != ',') p++;
					if (*p == ',') p++;
					int end = strtol(p, (char**)&p, 10);
					while (*p && *p != ']') p++;
					if (*p == ']') p++;
					change_range(start, end + 1, AREA_NON_SPC, AREA_FREE);
				}
				while (*p && (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
			}
		}
		while (*p && *p != ',' && *p != '}') p++;
		if (*p == ',') p++;
	}
	metadata_changed = TRUE;
}
