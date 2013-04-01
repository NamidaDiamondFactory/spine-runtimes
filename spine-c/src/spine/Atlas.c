/*******************************************************************************
 * Copyright (c) 2013, Esoteric Software
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#include <spine/Atlas.h>
#include <ctype.h>
#include <spine/extension.h>

void _AtlasPage_init (AtlasPage* self, const char* name) {
	CONST_CAST(_AtlasPageVtable*, self->vtable) = NEW(_AtlasPageVtable);
	self->name = name; /* name is guaranteed to be memory we allocated. */
}

void _AtlasPage_deinit (AtlasPage* self) {
	FREE(self->vtable);
	FREE(self->name);
}

void AtlasPage_free (AtlasPage* self) {
	VTABLE(AtlasPage, self) ->free(self);
}

/**/

AtlasRegion* AtlasRegion_new () {
	return NEW(AtlasRegion) ;
}

void AtlasRegion_free (AtlasRegion* self) {
	FREE(self->name);
	FREE(self->splits);
	FREE(self->pads);
	FREE(self);
}

/**/

typedef struct {
	const char* begin;
	const char* end;
} Str;

static void trim (Str* str) {
	while (isspace(*str->begin) && str->begin < str->end)
		(str->begin)++;
	if (str->begin == str->end) return;
	str->end--;
	while (isspace(*str->end) && str->end >= str->begin)
		str->end--;
	str->end++;
}

/* Tokenize string without modification. Returns 0 on failure. */
static int readLine (const char* begin, const char* end, Str* str) {
	static const char* nextStart;
	if (begin) {
		nextStart = begin;
		return 1;
	}
	if (nextStart == end) return 0;
	str->begin = nextStart;

	/* Find next delimiter. */
	do {
		nextStart++;
	} while (nextStart != end && *nextStart != '\n');

	str->end = nextStart;
	trim(str);

	if (nextStart != end) nextStart++;
	return 1;
}

/* Moves str->begin past the first occurence of c. Returns 0 on failure. */
static int beginPast (Str* str, char c) {
	const char* begin = str->begin;
	while (1) {
		char lastSkippedChar = *begin;
		if (begin == str->end) return 0;
		begin++;
		if (lastSkippedChar == c) break;
	}
	str->begin = begin;
	return 1;
}

/* Returns 0 on failure. */
static int readValue (const char* end, Str* str) {
	readLine(0, end, str);
	if (!beginPast(str, ':')) return 0;
	trim(str);
	return 1;
}

/* Returns the number of tuple values read (2, 4, or 0 for failure). */
static int readTuple (const char* end, Str tuple[]) {
	Str str;
	readLine(0, end, &str);
	if (!beginPast(&str, ':')) return 0;
	int i = 0;
	for (i = 0; i < 3; ++i) {
		tuple[i].begin = str.begin;
		if (!beginPast(&str, ',')) {
			if (i == 0) return 0;
			break;
		}
		tuple[i].end = str.begin - 2;
		trim(&tuple[i]);
	}
	tuple[i].begin = str.begin;
	tuple[i].end = str.end;
	trim(&tuple[i]);
	return i + 1;
}

static char* mallocString (Str* str) {
	int length = str->end - str->begin;
	char* string = MALLOC(char, length + 1);
	memcpy(string, str->begin, length);
	string[length] = '\0';
	return string;
}

static int indexOf (const char** array, int count, Str* str) {
	int length = str->end - str->begin;
	int i;
	for (i = count - 1; i >= 0; i--)
		if (strncmp(array[i], str->begin, length) == 0) return i;
	return -1;
}

static int equals (Str* str, const char* other) {
	return strncmp(other, str->begin, str->end - str->begin) == 0;
}

static int toInt (Str* str) {
	return strtol(str->begin, (char**)&str->end, 10);
}

static Atlas* abortAtlas (Atlas* self) {
	Atlas_free(self);
	return 0;
}

static const char* formatNames[] = {"Alpha", "Intensity", "LuminanceAlpha", "RGB565", "RGBA4444", "RGB888", "RGBA8888"};
static const char* textureFilterNames[] = {"Nearest", "Linear", "MipMap", "MipMapNearestNearest", "MipMapLinearNearest",
		"MipMapNearestLinear", "MipMapLinearLinear"};

Atlas* Atlas_readAtlas (const char* begin, unsigned long length) {
	const char* end = begin + length;

	Atlas* self = NEW(Atlas);

	AtlasPage *page = 0;
	AtlasPage *lastPage = 0;
	AtlasRegion *lastRegion = 0;
	Str str;
	Str tuple[4];
	readLine(begin, 0, 0);
	while (readLine(0, end, &str)) {
		if (str.end - str.begin == 0) {
			page = 0;
		} else if (!page) {
			page = AtlasPage_new(mallocString(&str));
			if (lastPage)
				lastPage->next = page;
			else
				self->pages = page;
			lastPage = page;

			if (!readValue(end, &str)) return abortAtlas(self);
			page->format = (AtlasFormat)indexOf(formatNames, 7, &str);

			if (!readTuple(end, tuple)) return abortAtlas(self);
			page->minFilter = (AtlasFilter)indexOf(textureFilterNames, 7, tuple);
			page->magFilter = (AtlasFilter)indexOf(textureFilterNames, 7, tuple + 1);

			if (!readValue(end, &str)) return abortAtlas(self);
			if (!equals(&str, "none")) {
				page->uWrap = *str.begin == 'x' ? ATLAS_REPEAT : (*str.begin == 'y' ? ATLAS_CLAMPTOEDGE : ATLAS_REPEAT);
				page->vWrap = *str.begin == 'x' ? ATLAS_CLAMPTOEDGE : (*str.begin == 'y' ? ATLAS_REPEAT : ATLAS_REPEAT);
			}
		} else {
			AtlasRegion *region = AtlasRegion_new();
			if (lastRegion)
				lastRegion->next = region;
			else
				self->regions = region;
			lastRegion = region;

			region->page = page;
			region->name = mallocString(&str);

			if (!readValue(end, &str)) return abortAtlas(self);
			region->rotate = equals(&str, "true");

			if (readTuple(end, tuple) != 2) return abortAtlas(self);
			region->x = toInt(tuple);
			region->y = toInt(tuple + 1);

			if (readTuple(end, tuple) != 2) return abortAtlas(self);
			region->width = toInt(tuple);
			region->height = toInt(tuple + 1);

			int count;
			if (!(count = readTuple(end, tuple))) return abortAtlas(self);
			if (count == 4) { /* split is optional */
				region->splits = MALLOC(int, 4);
				region->splits[0] = toInt(tuple);
				region->splits[1] = toInt(tuple + 1);
				region->splits[2] = toInt(tuple + 2);
				region->splits[3] = toInt(tuple + 3);

				if (!(count = readTuple(end, tuple))) return abortAtlas(self);
				if (count == 4) { /* pad is optional, but only present with splits */
					region->pads = MALLOC(int, 4);
					region->pads[0] = toInt(tuple);
					region->pads[1] = toInt(tuple + 1);
					region->pads[2] = toInt(tuple + 2);
					region->pads[3] = toInt(tuple + 3);

					if (!readTuple(end, tuple)) return abortAtlas(self);
				}
			}

			region->originalWidth = toInt(tuple);
			region->originalHeight = toInt(tuple + 1);

			readTuple(end, tuple);
			region->offsetX = (float)toInt(tuple);
			region->offsetY = (float)toInt(tuple + 1);

			if (!readValue(end, &str)) return abortAtlas(self);
			region->index = toInt(&str);
		}
	}

	return self;
}

Atlas* Atlas_readAtlasFile (const char* path) {
	int length;
	const char* data = _Util_readFile(path, &length);
	if (!data) return 0;
	Atlas* atlas = Atlas_readAtlas(data, length);
	FREE(data);
	return atlas;
}

void Atlas_free (Atlas* self) {
	AtlasPage* page = self->pages;
	while (page) {
		AtlasPage* nextPage = page->next;
		AtlasPage_free(page);
		page = nextPage;
	}

	AtlasRegion* region = self->regions;
	while (region) {
		AtlasRegion* nextRegion = region->next;
		AtlasRegion_free(region);
		region = nextRegion;
	}

	FREE(self);
}

AtlasRegion* Atlas_findRegion (const Atlas* self, const char* name) {
	AtlasRegion* region = self->regions;
	while (region) {
		if (strcmp(region->name, name) == 0) return region;
		region = region->next;
	}
	return 0;
}
