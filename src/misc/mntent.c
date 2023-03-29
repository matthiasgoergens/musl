#include <stdio.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <limits.h>
#include "stdio_impl.h"

static char *internal_buf;
static size_t internal_bufsize;

#define SENTINEL (char *)&internal_buf

FILE *setmntent(const char *name, const char *mode)
{
	return fopen(name, mode);
}

int endmntent(FILE *f)
{
	if (f) fclose(f);
	return 1;
}

static char* decode(char* buf) {
	char* src = buf;
	char* dest = buf;
	while (1) {
		char* next_src = __strchrnul(src, '\\');
		int offset = next_src - src;
		memmove(dest, src, offset);
		src = next_src;
		dest += offset;

		if(*src == '\0') {
			*dest = *src;
			return buf;
		}
		src++;

		const char *replacements =
			"\040"	"040"	"\0"  // space
			"\011"	"011"	"\0"  // tab
			"\012"	"012"	"\0"  // newline
			"\134"	"134"	"\0"  // backslash
			"\\"	"\\"	"\0"
			// Fallback for unrecognized escape sequence,
			// copy literally:
			"\\"	"";
		while(1) {
			char c = *replacements++;
			size_t n = strlen(replacements);
			if (strncmp(src, replacements, n) == 0) {
				*dest++ = c;
				src += n;
				break;
			}
			replacements += n+1;
		}
	}
}

struct mntent *getmntent_r(FILE *f, struct mntent *mnt, char *linebuf, int buflen)
{
	int n[8], use_internal = (linebuf == SENTINEL);
	size_t len, i;

	mnt->mnt_freq = 0;
	mnt->mnt_passno = 0;

	do {
		if (use_internal) {
			getline(&internal_buf, &internal_bufsize, f);
			linebuf = internal_buf;
		} else {
			fgets(linebuf, buflen, f);
		}
		if (feof(f) || ferror(f)) return 0;
		if (!strchr(linebuf, '\n')) {
			fscanf(f, "%*[^\n]%*[\n]");
			errno = ERANGE;
			return 0;
		}

		len = strlen(linebuf);
		if (len > INT_MAX) continue;
		for (i = 0; i < sizeof n / sizeof *n; i++) n[i] = len;
		sscanf(linebuf, " %n%*s%n %n%*s%n %n%*s%n %n%*s%n %d %d",
			n, n+1, n+2, n+3, n+4, n+5, n+6, n+7,
			&mnt->mnt_freq, &mnt->mnt_passno);
	} while (linebuf[n[0]] == '#' || n[1]==len);

	linebuf[n[1]] = 0;
	linebuf[n[3]] = 0;
	linebuf[n[5]] = 0;
	linebuf[n[7]] = 0;

	mnt->mnt_fsname = decode(linebuf+n[0]);
	mnt->mnt_dir = decode(linebuf+n[2]);
	mnt->mnt_type = decode(linebuf+n[4]);
	mnt->mnt_opts = decode(linebuf+n[6]);

	return mnt;
}

struct mntent *getmntent(FILE *f)
{
	static struct mntent mnt;
	return getmntent_r(f, &mnt, SENTINEL, 0);
}

static int escape_and_write_string(FILE *f, const char* str)
{
	const char* replace_me = "\040\011\012\\";
	char c;
	int error_occured = 0;
	while(str && !error_occured && (c = *str++) != 0) {
		if (NULL == strchr(replace_me, c)) {
			error_occured = putc_unlocked(c, f) < 0;
		} else {
			error_occured =
				(0 > putc_unlocked('\\', f))
				|| (0 > putc_unlocked('0' + (3 & (c >> 6)), f))
				|| (0 > putc_unlocked('0' + (7 & (c >> 3)), f))
				|| (0 > putc_unlocked('0' + (7 & (c >> 0)), f));
		}
	}
	return error_occured || (0 > putc_unlocked('\t', f));
}

int addmntent(FILE *f, const struct mntent *mnt)
{
	if (fseek(f, 0, SEEK_END)) return 1;
	FLOCK(f);
	int error_occured =
		escape_and_write_string(f, mnt->mnt_fsname)
		|| escape_and_write_string(f, mnt->mnt_dir)
		|| escape_and_write_string(f, mnt->mnt_type)
		|| escape_and_write_string(f, mnt->mnt_opts)
		|| (0 > fprintf(f, "%d\t%d\n",
			mnt->mnt_freq, mnt->mnt_passno));
	FUNLOCK(f);
	return error_occured;
}

char *hasmntopt(const struct mntent *mnt, const char *opt)
{
	return strstr(mnt->mnt_opts, opt);
}
