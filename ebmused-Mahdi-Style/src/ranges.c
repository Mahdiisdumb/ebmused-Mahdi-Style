#include <stdlib.h>
#include <string.h>
#include "ebmusv2.h"
#include "misc.h"

// JSON export/import for range data
void export_ranges_json(FILE *f) {
    fprintf(f, "{\n");
    fprintf(f, "\"area_count\": %d,\n", area_count);
    fprintf(f, "\"areas\": [\n");
    for (int i = 0; i < area_count; i++) {
        fprintf(f, "{\"address\": %d, \"pack\": %d}", areas[i].address, areas[i].pack);
        if (i < area_count - 1) fprintf(f, ",");
        fprintf(f, "\n");
    }
    fprintf(f, "]\n}\n");
}

BOOL import_ranges_json(FILE *f) {
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "\"area_count\":")) {
            sscanf(line, " \"area_count\": %d,", &area_count);
            areas = realloc(areas, sizeof(struct area) * area_count);
        } else if (strstr(line, "\"areas\":")) {
            for (int i = 0; i < area_count; i++) {
                fgets(line, sizeof(line), f);
                sscanf(line, " {\"address\": %d, \"pack\": %d}", &areas[i].address, &areas[i].pack);
            }
        }
    }
    return TRUE;
}

int area_count;
struct area *areas;

void init_areas() {
	area_count = 2;
	areas = malloc(sizeof(struct area) * 2);
	areas[0].address = -2147483647 - 1;	// -1 so compilers like Visual Studio won't interpret the - as a unary operator
	areas[0].pack = AREA_NOT_IN_FILE;
	areas[1].address = 2147483647;
	areas[1].pack = AREA_END;
}

static void split_area(int i, int address) {
	struct area *a = array_insert(&areas, &area_count, sizeof(struct area), i);
	a->address = address;
	a->pack = (a-1)->pack;
}

void change_range(int start, int end, int from, int to) {
	int i = 0;

	while (areas[i].address < start) i++;
	if (areas[i].address != start && areas[i-1].pack == from)
		split_area(i, start);

	while (areas[i].address < end) {
		if (areas[i+1].address > end)
			split_area(i+1, end);

		if (areas[i].pack == from) {
			areas[i].pack = to;
			if (areas[i-1].pack == to) {
				memmove(&areas[i], &areas[i+1], (--area_count - i) * sizeof(struct area));
				i--;
			}
			if (areas[i+1].pack == to) {
				memmove(&areas[i+1], &areas[i+2], (--area_count - (i + 1)) * sizeof(struct area));
			}
		}
		i++;
	}
}

// Determine if a range is OK to save a pack at. (i.e. it contains
// only free areas and the current pack itself
int check_range(int start, int end, int pack) {
	int i = 0;
	while (areas[i+1].address <= start) i++;
	while (areas[i].address < end) {
		if (areas[i].pack != AREA_FREE && areas[i].pack != pack)
			return areas[i].pack;
		i++;
	}
	return AREA_FREE;
}
