#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define BOM_UTF8 0x00bfbbefu

// TODO: add offsets and size for the userid and message data
struct mapping {
	uint64_t offset_timestamp;
	uint64_t size_timestamp;
	uint64_t offset_user;
	uint64_t size_user;
	uint64_t offset_chat;
	uint64_t size_chat;
	uint64_t timestamp;
	uint64_t _padding;
};

_Static_assert(64 == sizeof(struct mapping));

int main(int argc, char *argv[])
{
	if (argc < 1) {
		fprintf(stderr, "%s", "surprising command-line error (argc !> 0)\n");
		_exit(1);
	}
	else if ((NULL == argv) || (NULL == *argv) || (0 == (**argv))) {
		fprintf(stderr, "%s", "surprising command-line error (invalid argv)\n");
		_exit(1);
	}

	int show_help = 0;
	for (int i = 0; i != argc; ++i) {
		if (0 == i) {
			continue;
		}

		char const * const help = "--help";
		if (!strncmp(help, argv[i], sizeof(help))) {
			show_help = 1;
		} else {
			fprintf(stderr, "WARNING: ignoring foreign command-line argument: %s\n", argv[i]);
		}
	}

	if (show_help) {
		fprintf(stdout, "usage example: cat chat.txt | %s\n", argv[0]);
		_exit(0);
	}

	int64_t rc = 0;
	struct stat st = {};
	rc = fstat(STDIN_FILENO, &st);
	if (!S_ISFIFO(st.st_mode)) {
		fprintf(stderr,
			"%s %s",
			argv[0],
			"expects input to come from a pipe\n");
		fprintf(stderr, "usage example: cat chat.txt | %s\n", argv[0]);
		_exit(1);
	}

	errno = 0;
	rc = lseek(STDIN_FILENO, 0, SEEK_CUR);
	if (-1 == rc) {
		if (ESPIPE != errno) {
			fprintf(stderr,
				"%s %s",
				argv[0],
				"expects input to come from a stream-like pipe (a not seekable pipe)\n");
			fprintf(stderr, "usage example: cat chat.txt | %s\n", argv[0]);
			_exit(1);
		}
	} else {
		fprintf(stderr,
			"%s %s",
			argv[0],
			"expects input to come from a stream-like pipe (a not seekable pipe)\n");
		fprintf(stderr, "usage example: cat chat.txt | %s\n", argv[0]);
		_exit(1);
	}

	uint64_t const pagesz = sysconf(_SC_PAGESIZE);
	uint64_t len_mmap = (pagesz << 1);
	errno = 0;
	int fd = -1;
	int64_t of = 0;
	void *buff = mmap(NULL, len_mmap, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, fd, of);
	if (MAP_FAILED == buff) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	rc = madvise(buff, len_mmap, MADV_WILLNEED);
	if (-1 == rc) {
		fprintf(stderr, "%s", "error: buff mmap fast access failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	int64_t bytes_read = 0;
	do {
		errno = 0;
		rc = read(STDIN_FILENO, buff + bytes_read, pagesz);
		if (rc > 0) {
			bytes_read += rc;
			if ((len_mmap - bytes_read) <= pagesz) {
				buff = mremap(buff, len_mmap, (len_mmap << 1), MREMAP_MAYMOVE);
				if (MAP_FAILED == buff) {
					if (errno) {
						fprintf(stderr, "%s\n", strerror(errno));
					}
					_exit(1);
				}
				len_mmap <<= 1;
			}
		}
		else if (-1 == rc) {
			if (errno) {
				fprintf(stderr, "%s\n", strerror(errno));
			}
			_exit(1);
		}
	} while (rc);

	uint64_t len_chat = bytes_read;
#if DEVBUILD
	fprintf(stdout, "chat-length (bytes): %lu\n", len_chat);
	fprintf(stdout, "mmap-size (bytes): %lu\n", len_mmap);
#endif

	errno = 0;
	void *srcbuf = buff;
	void *dstbuf = mmap(NULL, len_mmap, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (MAP_FAILED == dstbuf) {
		fprintf(stderr, "%s", "error: destination chat memory mapping failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	rc = madvise(dstbuf, len_mmap, MADV_WILLNEED);
	if (-1 == rc) {
		fprintf(stderr, "%s", "error: dest mmap sequential access failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	uint64_t count = 0;
	uint64_t len_txt = 0;
	char unsigned *txt = srcbuf;
	char unsigned *dst = dstbuf;
	uint32_t const head_utf8 = 0x00ffffff & (
		(txt[3] << 24) |
		(txt[2] << 16) |
		(txt[1] <<  8) |
		(txt[0] <<  0)
	);
	uint16_t const head_utf16 = ((txt[1] << 8) | txt[0]);
	if (BOM_UTF8 == head_utf8) {
		txt += 3;
		count += 3;
	}
	else if ((0xfffeu == head_utf16) || (0xfeffu == head_utf16)) {
		fprintf(stderr, "%s", "error: unsupported utf-16\n");
		_exit(1);
	}
	// excludes emojis and other non-ASCII characters from the chat
	while (len_chat > count) {
		if (0x80u > (*txt)) {
			if (((*txt) < 0x0au)) {
				*dst = 0x20u;
			}
			else if (((*txt) >= 0x0bu) && ((*txt) < 0x20u)) {
				*dst = 0x20u;
			}
			else if (((*txt) == 0x22u) || ((*txt) == 0x27u)) { // NOTE: folds quotes to space for SQL
				*dst = 0x20u;
			}
			else if (((*txt) >= 0x41u) && ((*txt) < 0x5bu)) {
				*dst = (((*txt) - 0x41u) + 0x61u);
			}
			else if ((0x7fu == (*txt))) {
				*dst = 0x20u;
			}
			else {
				*dst = *txt;
			}
			txt += 1;
			dst += 1;
			count += 1;
			len_txt += 1;
		}
		else if (0xc2 > (*txt))  {
			fprintf(stderr, "%s", "error: invalid utf-8 prefix\n");
			_exit(1);
		}
		else if (0xe0u > (*txt)) {
			uint16_t const value = ((txt[1] << 8) | txt[0]);
			if ((value >= 0x80c3u) && (value < 0x86c3u)) {
				*dst = 'a';
				dst += 1;
				len_txt += 1;
			}
			else if ((value >= 0x88c3u) && (value < 0x8cc3u)) {
				*dst = 'e';
				dst += 1;
				len_txt += 1;
			}
			else if ((value >= 0x8cc3u) && (value < 0x90c3u)) {
				*dst = 'i';
				dst += 1;
				len_txt += 1;
			}
			else if ((value >= 0x92c3u) && (value < 0x97c3u)) {
				*dst = 'o';
				dst += 1;
				len_txt += 1;
			}
			else if ((value >= 0x99c3u) && (value < 0x9ec3u)) {
				*dst = 'u';
				dst += 1;
				len_txt += 1;
			}
			else if ((value >= 0xa0c3u) && (value < 0xa6c3u)) {
				*dst = 'a';
				dst += 1;
				len_txt += 1;
			}
			else if ((value >= 0xa8c3u) && (value < 0xacc3u)) {
				*dst = 'e';
				dst += 1;
				len_txt += 1;
			}
			else if ((value >= 0xacc3u) && (value < 0xb0c3u)) {
				*dst = 'i';
				dst += 1;
				len_txt += 1;
			}
			else if ((value == 0xb1c3u)) {
				*dst = 'n';
				dst += 1;
				len_txt += 1;
			}
			else if ((value >= 0xb2c3u) && (value < 0xb7c3u)) {
				*dst = 'o';
				dst += 1;
				len_txt += 1;
			}
			else if ((value >= 0xb9c3u) && (value < 0xbdc3u)) {
				*dst = 'u';
				dst += 1;
				len_txt += 1;
			}
			txt += 2;
			count += 2;
		}
		else if (0xf0u > (*txt)) {
			txt += 3;
			count += 3;
		}
		else {
			txt += 4;
			count += 4;
		}
	}
	if (len_chat != count) {
		fprintf(stderr, "%s", "error: bytes read and filesize mismatch\n");
		_exit(1);
	}
#if DEVBUILD
	else {
		fprintf(stdout, "%s %lu %s", "bytes-read:", count, "\n");
		fprintf(stdout, "%s %lu %s", "bytes-kept:", len_txt, "\n");
	}
#endif

	if (-1 == (rc = munmap(srcbuf, len_mmap))) {
		fprintf(stderr, "%s", "error: unmapping chat failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	srcbuf = NULL;

#if DEVBUILD
	dst = dstbuf;
	char const *normerr = "error: ascii normalization failed\n";
	char const *folderr = "error: ascii folding failed\n";
	char const *tliterr = "error: unicode to ascii transliteration failed\n";
	for (int i = 0; i != len_txt; ++i, ++dst) {
		if ((*dst < 0x0au)) {
			fprintf(stderr, "%s", normerr);
			fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
			_exit(1);
		}
		else if ((*dst >= 0x0bu) && (*dst < 0x20u)) {
			fprintf(stderr, "%s", normerr);
			fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
			_exit(1);
		}
		else if (((*dst) >= 0x41u) && ((*dst) < 0x5bu)) {
			fprintf(stderr, "%s", folderr);
			fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
			_exit(1);
		}
		else if (*dst >= 0x7fu) {
			fprintf(stderr, "%s", tliterr);
			fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
			_exit(1);
		}
	}

//EXPERIMENTAL TIMESTAMP DETECTION CODE
//
//- uses simple conditionals to locate timestamps
//- need to set the struct tm according to the AM and PM cases
//- for the time being the timestamp is shown on the console for verification
//- consider checking for the hyphen `-` as well
//- consider checking for the newline character preceeding the timestamp or the first-character in the buffer
//- consider using a regex in the future to account for the presence of the username followed by a colon `:`
//  to make it less likely to confuse the timestamp with the chat text
//- of course there's a certain degree of repetition that could be taken into account for refactoring but
//  right now this is exploratory code and I am fine with repetition

	dst = dstbuf;
	setenv("TZ", "EST-5:00:00", 1); // sets the timezone for the timestamp data in the chat
	int64_t sec = 0;
	int64_t tmin = 0;
	int64_t hour = 0;
	int64_t mday = 0;
	int64_t mon = 0;
	int64_t year = 0;
	int64_t encoded_time = 0;
	int64_t prev_timestamp = 0;
	int64_t const isdst = 0;
	uint64_t timestamps = 0;
	uint64_t offset = 0;
	uint64_t const offset_mapbase = ((len_txt + 0x3fu) & ~0x3fu);
	uint64_t offset_map = 0;
	uint32_t lineno = 0;
	uint32_t sz_timestamp = 0;
	void *vptr = NULL;
	char *endptr = NULL;
	char *nl = NULL;
	char *dm = NULL;
	struct tm timestamp = {};
	struct tm * const tp = &timestamp;
	struct mapping const *map = (dstbuf + offset_mapbase);
	char unsigned mmddyy[32];
	memset(mmddyy, 0, sizeof(mmddyy));
	for (int i = 0; i != len_txt; ++i, ++offset) {
	    dst = dstbuf; // NOTE: if the mmap base address moves pointers become invalidated on growth
	    if ((dst[offset + 0] >= 0x30u) && (dst[offset + 0] < 0x3au)) {
		if ('/' == dst[offset + 1]) {

		    if ((dst[offset + 2] >= 0x30u) && (dst[offset + 2] < 0x3au)) {
			if ('/' == dst[offset + 3]) {

			    if (
				    (dst[offset + 4] >= 0x30u) && (dst[offset + 4] < 0x3au) &&
				    (dst[offset + 5] >= 0x30u) && (dst[offset + 5] < 0x3au) &&
				    (dst[offset + 6] == ',') &&
				    (dst[offset + 7] == ' ') && (
					(
					 (dst[offset + 8] >= 0x30u) && (dst[offset + 8] < 0x3au) &&
					 (dst[offset + 9] == ':') &&
					 (dst[offset + 10] >= 0x30u) && (dst[offset + 10] < 0x3au) &&
					 (dst[offset + 11] >= 0x30u) && (dst[offset + 11] < 0x3au)
					) ||
					(
					 (dst[offset + 8] >= 0x30u) && (dst[offset + 8] < 0x3au) &&
					 (dst[offset + 9] >= 0x30u) && (dst[offset + 9] < 0x3au) &&
					 (dst[offset + 10] == ':') &&
					 (dst[offset + 11] >= 0x30u) && (dst[offset + 11] < 0x3au) &&
					 (dst[offset + 12] >= 0x30u) && (dst[offset + 12] < 0x3au)
					)
				    )
			       ) {

				errno = 0;
				vptr = &dst[offset + 0];
				endptr = NULL;
				lineno = (1 + (__LINE__));
				mon = (strtol(vptr, &endptr, 10) - 1);
				if (errno) {
				    goto err;
				}
				else if ((!*endptr) || ('/' != endptr[0])) {
				    goto err_uxchar_timestamp;
				}
				else if (1 != (((void*) endptr) - vptr)) {
				    goto err_uxlen_timestamp;
				}
				else if (!(mon >= 0 && mon < 12)) {
				    goto err_month_timestamp;
				}

				errno = 0;
				vptr = &dst[offset + 2];
				endptr = NULL;
				lineno = (1 + (__LINE__));
				mday = strtol(vptr, &endptr, 10);
				if (errno) {
				    goto err;
				}
				else if ((!*endptr) || ('/' != endptr[0])) {
				    goto err_uxchar_timestamp;
				}
				else if (1 != (((void*) endptr) - vptr)) {
				    goto err_uxlen_timestamp;
				}
				else if (!((mday >= 1) && (mday < 32))) {
				    goto err_day_timestamp;
				}

				errno = 0;
				vptr = &dst[offset + 4];
				endptr = NULL;
				lineno = (1 + (__LINE__));
				year = (strtol(vptr, &endptr, 10) + (2000 - 1900));
				if (errno) {
				    goto err;
				}
				else if ((!*endptr) || (',' != endptr[0])) {
				    goto err_uxchar_timestamp;
				}
				else if (2 != (((void*) endptr) - vptr)) {
				    goto err_uxlen_timestamp;
				}
				else if (!((year >= (2026 - 1900)) && (year < (2038 - 1900)))) {
				    goto err_year_timestamp;
				}

				errno = 0;
				vptr = &dst[offset + 8];
				endptr = NULL;
				lineno = (1 + (__LINE__));
				hour = strtol(vptr, &endptr, 10);
				if (errno) {
				    goto err;
				}
				else if ((!*endptr) || (':' != endptr[0])) {
				    goto err_uxchar_timestamp;
				}
				else if (
					(1 != (((void*) endptr) - vptr)) &&
					(2 != (((void*) endptr) - vptr))
					) {
				    goto err_uxlen_timestamp;
				}
				else if (!((hour >= 0) && (hour < 24))) {
				    goto err_hour_timestamp;
				}

				errno = 0;
				vptr = (1 + endptr);
				endptr = NULL;
				lineno = (1 + (__LINE__));
				tmin = strtol(vptr, &endptr, 10);
				if (errno) {
				    goto err;
				}
				else if ((!*endptr) || (('a' != endptr[0]) && ('p' != endptr[0]))) {
				    goto err_uxchar_timestamp;
				}
				else if (
					(1 != (((void*) endptr) - vptr)) &&
					(2 != (((void*) endptr) - vptr))
					) {
				    goto err_uxlen_timestamp;
				}
				else if (!((tmin >= 0) && (tmin < 60))) {
				    goto err_min_timestamp;
				}

				if ('p' == endptr[0]) {
				    if (12 != hour) hour +=12;
				}
				if (!((hour >= 0) && (hour < 24))) {
				    goto err_hour_timestamp;
				}

				tp->tm_sec = sec;
				tp->tm_min = tmin;
				tp->tm_hour = hour;
				tp->tm_mday = mday;
				tp->tm_mon = mon;
				tp->tm_year = year;
				tp->tm_isdst = isdst;

				errno = 0;
				lineno = (1 + __LINE__);
				encoded_time = mktime(tp);
				if (errno) {
					goto err_encoding_timestamp;
				}

				vptr = &dst[offset + 0];
				nl = strstr(vptr, "\n");
				dm = strstr(vptr, "-");
				if (nl && dm) {
					if (dm < nl) {
						void * const vmap = dstbuf + offset_mapbase + offset_map;
						struct mapping * const map = vmap;
						map->offset_timestamp = (vptr - dstbuf);
						map->size_timestamp = (((void*) dm) - vptr);
						if (prev_timestamp >= encoded_time) {
							encoded_time = (1 + prev_timestamp);
						}
						prev_timestamp = encoded_time;
						map->timestamp = encoded_time;
						offset_map += sizeof(*map);
						++timestamps;

						if (len_mmap - (offset_mapbase + offset_map) <= pagesz) {
							dstbuf = mremap(dstbuf, len_mmap, (len_mmap << 1), MREMAP_MAYMOVE);
							len_mmap <<= 1;
							dst = dstbuf;
						}
					}
				}

				uint16_t const AntePostMeridiemValue = ((dst[offset + 13] << 8) | dst[offset + 12]);
				if (
					(0x6d61u == AntePostMeridiemValue) ||
					(0x6d70u == AntePostMeridiemValue)
				   )
				{
				    sz_timestamp = 14;
				    memcpy(mmddyy, dst, sz_timestamp);
				    mmddyy[sz_timestamp] = 0;
				}
				else {
				    sz_timestamp = 15;
				    memcpy(mmddyy, dst, sz_timestamp);
				    mmddyy[sz_timestamp] = 0;
				}
				//fprintf(stdout, "timestamp: %s mm/dd/yy, hh:mm %ld/%ld/%ld, %.2ld:%.2ld encoding: %ld\n", mmddyy, mon, mday, year, hour, tmin, encoded_time);
				memset(mmddyy, 0, sizeof(mmddyy));
			    }
			}
			else if ((dst[offset + 3] >= 0x30u) && (dst[offset + 3] < 0x3au)) {
			    if ('/' == dst[offset + 4]) {

				if (
					(dst[offset + 5] >= 0x30u) && (dst[offset + 5] < 0x3au) &&
					(dst[offset + 6] >= 0x30u) && (dst[offset + 6] < 0x3au) &&
					(dst[offset + 7] == ',') &&
					(dst[offset + 8] == ' ') && (
					    (
					     (dst[offset + 9] >= 0x30u) && (dst[offset + 9] < 0x3au) &&
					     (dst[offset + 10] == ':') &&
					     (dst[offset + 11] >= 0x30u) && (dst[offset + 11] < 0x3au) &&
					     (dst[offset + 12] >= 0x30u) && (dst[offset + 12] < 0x3au)
					    ) ||
					    (
					     (dst[offset + 9] >= 0x30u) && (dst[offset + 9] < 0x3au) &&
					     (dst[offset + 10] >= 0x30u) && (dst[offset + 10] < 0x3au) &&
					     (dst[offset + 11] == ':') &&
					     (dst[offset + 12] >= 0x30u) && (dst[offset + 12] < 0x3au) &&
					     (dst[offset + 13] >= 0x30u) && (dst[offset + 13] < 0x3au)
					    )
					)
				   ) {

				    errno = 0;
				    vptr = &dst[offset + 0];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    mon = (strtol(vptr, &endptr, 10) - 1);
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || ('/' != endptr[0])) {
					goto err_uxchar_timestamp;
				    }
				    else if (1 != (((void*) endptr) - vptr)) {
					goto err_uxlen_timestamp;
				    }
				    else if (!(mon >= 0 && mon < 12)) {
					goto err_month_timestamp;
				    }

				    errno = 0;
				    vptr = &dst[offset + 2];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    mday = strtol(vptr, &endptr, 10);
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || ('/' != endptr[0])) {
					goto err_uxchar_timestamp;
				    }
				    else if (2 != (((void*) endptr) - vptr)) {
					goto err_uxlen_timestamp;
				    }
				    else if (!((mday >= 1) && (mday < 32))) {
					goto err_day_timestamp;
				    }

				    errno = 0;
				    vptr = &dst[offset + 5];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    year = (strtol(vptr, &endptr, 10) + (2000 - 1900));
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || (',' != endptr[0])) {
					goto err_uxchar_timestamp;
				    }
				    else if (2 != (((void*) endptr) - vptr)) {
					goto err_uxlen_timestamp;
				    }
				    else if (!((year >= (2026 - 1900)) && (year < (2038 - 1900)))) {
					goto err_year_timestamp;
				    }

				    errno = 0;
				    vptr = &dst[offset + 9];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    hour = strtol(vptr, &endptr, 10);
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || (':' != endptr[0])) {
					goto err_uxchar_timestamp;
				    }
				    else if (
					    (1 != (((void*) endptr) - vptr)) &&
					    (2 != (((void*) endptr) - vptr))
					    ) {
					goto err_uxlen_timestamp;
				    }
				    else if (!((hour >= 0) && (hour < 24))) {
					goto err_hour_timestamp;
				    }

				    errno = 0;
				    vptr = (1 + endptr);
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    tmin = strtol(vptr, &endptr, 10);
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || (('a' != endptr[0]) && ('p' != endptr[0]))) {
					goto err_uxchar_timestamp;
				    }
				    else if (
					    (1 != (((void*) endptr) - vptr)) &&
					    (2 != (((void*) endptr) - vptr))
					    ) {
					goto err_uxlen_timestamp;
				    }
				    else if (!((tmin >= 0) && (tmin < 60))) {
					goto err_min_timestamp;
				    }

				    if ('p' == endptr[0]) {
					if (12 != hour) hour +=12;
				    }
				    if (!((hour >= 0) && (hour < 24))) {
					goto err_hour_timestamp;
				    }

				    tp->tm_sec = sec;
				    tp->tm_min = tmin;
				    tp->tm_hour = hour;
				    tp->tm_mday = mday;
				    tp->tm_mon = mon;
				    tp->tm_year = year;
				    tp->tm_isdst = isdst;

				    errno = 0;
				    lineno = (1 + __LINE__);
				    encoded_time = mktime(tp);
				    if (-1 == encoded_time) {
					    goto err_encoding_timestamp;
				    }

				    vptr = &dst[offset + 0];
				    nl = strstr(vptr, "\n");
				    dm = strstr(vptr, "-");
				    if (nl && dm) {
					    if (dm < nl) {
						    void * const vmap = dstbuf + offset_mapbase + offset_map;
						    struct mapping * const map = vmap;
						    map->offset_timestamp = (vptr - dstbuf);
						    map->size_timestamp = (((void*) dm) - vptr);
						    if (prev_timestamp >= encoded_time) {
							    encoded_time = (1 + prev_timestamp);
						    }
						    prev_timestamp = encoded_time;
						    map->timestamp = encoded_time;
						    offset_map += sizeof(*map);
						    ++timestamps;

						    if (len_mmap - (offset_mapbase + offset_map) <= pagesz) {
							    dstbuf = mremap(dstbuf, len_mmap, (len_mmap << 1), MREMAP_MAYMOVE);
							    len_mmap <<= 1;
							    dst = dstbuf;
						    }
					    }
				    }

				    uint16_t const AntePostMeridiemValue = ((dst[offset + 14] << 8) | dst[offset + 13]);
				    if (
					    (0x6d61u == AntePostMeridiemValue) ||
					    (0x6d70u == AntePostMeridiemValue)
				       ) {
					sz_timestamp = 15;
					memcpy(mmddyy, dst, sz_timestamp);
					mmddyy[sz_timestamp] = 0;
				    }
				    else {
					sz_timestamp = 16;
					memcpy(mmddyy, dst, sz_timestamp);
					mmddyy[sz_timestamp] = 0;
				    }
				    //fprintf(stdout, "timestamp: %s mm/dd/yy, hh:mm %ld/%ld/%ld, %.2ld:%.2ld encoding: %ld\n", mmddyy, mon, mday, year, hour, tmin, encoded_time);
				    memset(mmddyy, 0, sizeof(mmddyy));
				}
			    }
			}
		    }
		}
		else if ((dst[offset + 1] >= 0x30u) && (dst[offset + 1] < 0x3au)) {
		    if ('/' == dst[offset + 2]) {

			if ((dst[offset + 3] >= 0x30u) && (dst[offset + 3] < 0x3au)) {
			    if ('/' == dst[offset + 4]) {

				if (
					(dst[offset + 5] >= 0x30u) && (dst[offset + 5] < 0x3au) &&
					(dst[offset + 6] >= 0x30u) && (dst[offset + 6] < 0x3au) &&
					(dst[offset + 7] == ',') &&
					(dst[offset + 8] == ' ') && (
					    (
					     (dst[offset + 9] >= 0x30u) && (dst[offset + 9] < 0x3au) &&
					     (dst[offset + 10] == ':') &&
					     (dst[offset + 11] >= 0x30u) && (dst[offset + 11] < 0x3au) &&
					     (dst[offset + 12] >= 0x30u) && (dst[offset + 12] < 0x3au)
					    ) ||
					    (
					     (dst[offset + 9] >= 0x30u) && (dst[offset + 9] < 0x3au) &&
					     (dst[offset + 10] >= 0x30u) && (dst[offset + 10] < 0x3au) &&
					     (dst[offset + 11] == ':') &&
					     (dst[offset + 12] >= 0x30u) && (dst[offset + 12] < 0x3au) &&
					     (dst[offset + 13] >= 0x30u) && (dst[offset + 13] < 0x3au)
					    )
					)
				   ) {

				    errno = 0;
				    vptr = &dst[offset + 0];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    mon = (strtol(vptr, &endptr, 10) - 1);
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || ('/' != endptr[0])) {
					goto err_uxchar_timestamp;
				    }
				    else if (2 != (((void*) endptr) - vptr)) {
					goto err_uxlen_timestamp;
				    }
				    else if (!(mon >= 0 && mon < 12)) {
					goto err_month_timestamp;
				    }

				    errno = 0;
				    vptr = &dst[offset + 3];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    mday = strtol(vptr, &endptr, 10);
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || ('/' != endptr[0])) {
					goto err_uxchar_timestamp;
				    }
				    else if (1 != (((void*) endptr) - vptr)) {
					goto err_uxlen_timestamp;
				    }
				    else if (!((mday >= 1) && (mday < 32))) {
					goto err_day_timestamp;
				    }

				    errno = 0;
				    vptr = &dst[offset + 5];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    year = (strtol(vptr, &endptr, 10) + (2000 - 1900));
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || (',' != endptr[0])) {
					goto err_uxchar_timestamp;
				    }
				    else if (2 != (((void*) endptr) - vptr)) {
					goto err_uxlen_timestamp;
				    }
				    else if (!((year >= (2026 - 1900)) && year < (2038 - 1900))) {
					goto err_year_timestamp;
				    }

				    errno = 0;
				    vptr = &dst[offset + 9];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    hour = strtol(vptr, &endptr, 10);
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || (':' != endptr[0])) {
					goto err_uxchar_timestamp;
				    }
				    else if (
					    (1 != (((void*) endptr) - vptr)) &&
					    (2 != (((void*) endptr) - vptr))
					    ) {
					goto err_uxlen_timestamp;
				    }
				    else if (!((hour >= 0) && (hour < 24))) {
					goto err_hour_timestamp;
				    }

				    errno = 0;
				    vptr = (1 + endptr);
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    tmin = strtol(vptr, &endptr, 10);
				    if (errno) {
					goto err;
				    }
				    else if ((!*endptr) || (('a' != endptr[0]) && ('p' != endptr[0]))) {
					goto err_uxchar_timestamp;
				    }
				    else if (
					    (1 != (((void*) endptr) - vptr)) &&
					    (2 != (((void*) endptr) - vptr))
					    ) {
					goto err_uxlen_timestamp;
				    }
				    else if (!((tmin >= 0) && (tmin < 60))) {
					goto err_min_timestamp;
				    }

				    if ('p' == endptr[0]) {
					if (12 != hour) hour +=12;
				    }
				    if (!((hour >= 0) && (hour < 24))) {
					goto err_hour_timestamp;
				    }

				    tp->tm_sec = sec;
				    tp->tm_min = tmin;
				    tp->tm_hour = hour;
				    tp->tm_mday = mday;
				    tp->tm_mon = mon;
				    tp->tm_year = year;
				    tp->tm_isdst = isdst;

				    errno = 0;
				    lineno = (1 + __LINE__);
				    encoded_time = mktime(tp);
				    if (-1 == encoded_time) {
					    goto err_encoding_timestamp;
				    }

				    vptr = &dst[offset + 0];
				    nl = strstr(vptr, "\n");
				    dm = strstr(vptr, "-");
				    if (nl && dm) {
					    if (dm < nl) {
						    void * const vmap = dstbuf + offset_mapbase + offset_map;
						    struct mapping * const map = vmap;
						    map->offset_timestamp = (vptr - dstbuf);
						    map->size_timestamp = (((void*) dm) - vptr);
						    if (prev_timestamp >= encoded_time) {
							    encoded_time = (1 + prev_timestamp);
						    }
						    prev_timestamp = encoded_time;
						    map->timestamp = encoded_time;
						    offset_map += sizeof(*map);
						    ++timestamps;

						    if (len_mmap - (offset_mapbase + offset_map) <= pagesz) {
							    dstbuf = mremap(dstbuf, len_mmap, (len_mmap << 1), MREMAP_MAYMOVE);
							    len_mmap <<= 1;
							    dst = dstbuf;
						    }
					    }
				    }

				    uint16_t const AntePostMeridiemValue = ((dst[offset + 14] << 8) | dst[offset + 13]);
				    if (
					    (0x6d61u == AntePostMeridiemValue) ||
					    (0x6d70u == AntePostMeridiemValue)
				       ) {
					sz_timestamp = 15;
					memcpy(mmddyy, dst, sz_timestamp);
					mmddyy[sz_timestamp] = 0;
				    }
				    else {
					sz_timestamp = 16;
					memcpy(mmddyy, dst, sz_timestamp);
					mmddyy[sz_timestamp] = 0;
				    }
				    //fprintf(stdout, "timestamp: %s mm/dd/yy, hh:mm %ld/%ld/%ld, %.2ld:%.2ld encoding: %ld\n", mmddyy, mon, mday, year, hour, tmin, encoded_time);
				    memset(mmddyy, 0, sizeof(mmddyy));
				}
			    }
			    else if ((dst[offset + 4] >= 0x30u) && (dst[offset + 4] < 0x3au)) {
				if ('/' == dst[offset + 5]) {

				    if (
					    (dst[offset + 6] >= 0x30u) && (dst[offset + 6] < 0x3au) &&
					    (dst[offset + 7] >= 0x30u) && (dst[offset + 7] < 0x3au) &&
					    (dst[offset + 8] == ',') &&
					    (dst[offset + 9] == ' ') && (
						(
						 (dst[offset + 10] >= 0x30u) && (dst[offset + 10] < 0x3au) &&
						 (dst[offset + 11] == ':') &&
						 (dst[offset + 12] >= 0x30u) && (dst[offset + 12] < 0x3au) &&
						 (dst[offset + 13] >= 0x30u) && (dst[offset + 13] < 0x3au)
						) ||
						(
						 (dst[offset + 10] >= 0x30u) && (dst[offset + 10] < 0x3au) &&
						 (dst[offset + 11] >= 0x30u) && (dst[offset + 11] < 0x3au) &&
						 (dst[offset + 12] == ':') &&
						 (dst[offset + 13] >= 0x30u) && (dst[offset + 13] < 0x3au) &&
						 (dst[offset + 14] >= 0x30u) && (dst[offset + 14] < 0x3au)
						)
					    )
				       ) {

					errno = 0;
					vptr = &dst[offset + 0];
					endptr = NULL;
					lineno = (1 + (__LINE__));
					mon = (strtol(vptr, &endptr, 10) - 1);
					if (errno) {
					    goto err;
					}
					else if ((!*endptr) || ('/' != endptr[0])) {
					    goto err_uxchar_timestamp;
					}
					else if (2 != (((void*) endptr) - vptr)) {
					    goto err_uxlen_timestamp;
					}
					else if (!(mon >= 0 && mon < 12)) {
					    goto err_month_timestamp;
					}

					errno = 0;
					vptr = &dst[offset + 3];
					endptr = NULL;
					lineno = (1 + (__LINE__));
					mday = strtol(vptr, &endptr, 10);
					if (errno) {
					    goto err;
					}
					else if ((!*endptr) || ('/' != endptr[0])) {
					    goto err_uxchar_timestamp;
					}
					else if (2 != (((void*) endptr) - vptr)) {
					    goto err_uxlen_timestamp;
					}
					else if (!((mday >= 1) && (mday < 32))) {
					    goto err_day_timestamp;
					}

					errno = 0;
					vptr = &dst[offset + 6];
					endptr = NULL;
					lineno = (1 + (__LINE__));
					year = (strtol(vptr, &endptr, 10) + (2000 - 1900));
					if (errno) {
					    goto err;
					}
					else if ((!*endptr) || (',' != endptr[0])) {
					    goto err_uxchar_timestamp;
					}
					else if (2 != (((void*) endptr) - vptr)) {
					    goto err_uxlen_timestamp;
					}
					else if (!((year >= (2026 - 1900)) && year < (2038 - 1900))) {
					    goto err_year_timestamp;
					}

					errno = 0;
					vptr = &dst[offset + 10];
					endptr = NULL;
					lineno = (1 + (__LINE__));
					hour = strtol(vptr, &endptr, 10);
					if (errno) {
					    goto err;
					}
					else if ((!*endptr) || (':' != endptr[0])) {
					    goto err_uxchar_timestamp;
					}
					else if (
						(1 != (((void*) endptr) - vptr)) &&
						(2 != (((void*) endptr) - vptr))
						) {
					    goto err_uxlen_timestamp;
					}
					else if (!((hour >= 0) && (hour < 24))) {
					    goto err_hour_timestamp;
					}

					errno = 0;
					vptr = (1 + endptr);
					endptr = NULL;
					lineno = (1 + (__LINE__));
					tmin = strtol(vptr, &endptr, 10);
					if (errno) {
					    goto err;
					}
					else if ((!*endptr) || (('a' != endptr[0]) && ('p' != endptr[0]))) {
					    goto err_uxchar_timestamp;
					}
					else if (
						(1 != (((void*) endptr) - vptr)) &&
						(2 != (((void*) endptr) - vptr))
						) {
					    goto err_uxlen_timestamp;
					}
					else if (!((tmin >= 0) && (tmin < 60))) {
					    goto err_min_timestamp;
					}

					if ('p' == endptr[0]) {
					    if (12 != hour) hour +=12;
					}
					if (!((hour >= 0) && (hour < 24))) {
					    goto err_hour_timestamp;
					}

					tp->tm_sec = sec;
					tp->tm_min = tmin;
					tp->tm_hour = hour;
					tp->tm_mday = mday;
					tp->tm_mon = mon;
					tp->tm_year = year;
					tp->tm_isdst = isdst;

					errno = 0;
					lineno = (1 + __LINE__);
					encoded_time = mktime(tp);
					if (-1 == encoded_time) {
						goto err_encoding_timestamp;
					}

					vptr = &dst[offset + 0];
					nl = strstr(vptr, "\n");
					dm = strstr(vptr, "-");
					if (nl && dm) {
						if (dm < nl) {
							void * const vmap = dstbuf + offset_mapbase + offset_map;
							struct mapping * const map = vmap;
							map->offset_timestamp = (vptr - dstbuf);
							map->size_timestamp = (((void*) dm) - vptr);
							if (prev_timestamp >= encoded_time) {
								encoded_time = (1 + prev_timestamp);
							}
							prev_timestamp = encoded_time;
							map->timestamp = encoded_time;
							offset_map += sizeof(*map);
							++timestamps;

							if (len_mmap - (offset_mapbase + offset_map) <= pagesz) {
								dstbuf = mremap(dstbuf, len_mmap, (len_mmap << 1), MREMAP_MAYMOVE);
								len_mmap <<= 1;
								dst = dstbuf;
							}
						}
					}

					uint16_t const AntePostMeridiemValue = ((dst[offset + 15] << 8) | dst[offset + 14]);
					if (
						(0x6d61u == AntePostMeridiemValue) ||
						(0x6d70u == AntePostMeridiemValue)
					   ) {
					    sz_timestamp = 16;
					    memcpy(mmddyy, dst, sz_timestamp);
					    mmddyy[sz_timestamp] = 0;
					}
					else {
					    sz_timestamp = 17;
					    memcpy(mmddyy, dst, sz_timestamp);
					    mmddyy[sz_timestamp] = 0;
					}
					//fprintf(stdout, "timestamp: %s mm/dd/yy, hh:mm %ld/%ld/%ld, %.2ld:%.2ld encoding: %ld\n", mmddyy, mon, mday, year, hour, tmin, encoded_time);
					memset(mmddyy, 0, sizeof(mmddyy));
				    }
				}
			    }
			}
		    }
		}
	    }
	}

	// updates the mapping array (timestamp, user, and chat messages)
	fprintf(stdout, "timestamps: %lu\n", timestamps);
	map = (dstbuf + offset_mapbase);
	prev_timestamp = 0;
	offset_map = 0;
	char unsigned user[32];
	char unsigned chat[64];
	for (uint32_t i = 0; i != timestamps; ++i, offset_map += sizeof(*map)) {
		void * const vmap = (dstbuf + (offset_mapbase + offset_map));
		struct mapping * const map = vmap;
		if (map->timestamp <= prev_timestamp) {
			fprintf(stderr, "%s", "error: timestamp increment\n");
			_exit(1);
		}
		prev_timestamp = map->timestamp;

		if (sizeof(mmddyy) > map->size_timestamp) {
			memset(mmddyy, 0, sizeof(mmddyy));
			memcpy(mmddyy, dstbuf + map->offset_timestamp, map->size_timestamp);
			void *vsep = strstr(dstbuf + map->offset_timestamp, "-");
			if (!vsep) {
				fprintf(stderr, "%s", "error: missing timestamp separator\n");
				_exit(1);
			}
			vsep += 2;
			char const * const messages = "messages";
			if (!strncmp(messages, vsep, sizeof(messages))) {
				continue;
			}

			void *vend = strstr(vsep, ":");
			if (!vend) {
				fprintf(stderr, "%s", "error: missing userdata\n");
				_exit(1);
			}

			// for new contacts whatsapp does not add the : before the next timestamp
			void *vnln = strstr(vsep, "\n");
			if (vnln < vend) {
				continue;
			}

			map->offset_user = (vsep - dstbuf);
			map->size_user = (vend - vsep);

			memset(user, 0, sizeof(user));
			memcpy(user, dstbuf + map->offset_user, map->size_user);

			++vend;
			void *vnxt = NULL;
			if ((timestamps - 1) == i) {
				vnxt = dstbuf + len_txt;
			} else {
				struct mapping const * const nextmap = (map + 1);
				vnxt = dstbuf + nextmap->offset_timestamp;
			}
			map->offset_chat = (vend - dstbuf);
			map->size_chat = (vnxt - vend);
			uint64_t const size = ((map->size_chat < sizeof(chat))
					? map->size_chat
					: sizeof(chat)
			);
			memset(chat, 0, sizeof(chat));
			memcpy(chat, dstbuf + map->offset_chat, size);
			chat[size - 1] = 0;
			uint64_t const timestamp = map->timestamp;
			//fprintf(stdout, "%s :: %lu ::  %s :: %s\n", mmddyy, timestamp, user, chat);
		}
		else {
			fprintf(stdout, "%s", "would overrun timestamp placeholder\n");
		}
	}
#endif
	uint64_t bytes_written = 0;
	do {
		errno = 0;
		rc = write(STDOUT_FILENO, dstbuf + bytes_written, len_txt - bytes_written);
		if (rc > 0) {
			bytes_written += rc;
		}
		else if (-1 == rc) {
			if (errno) {
				if (EINTR != errno) {
					fprintf(stderr, "%s\n", strerror(errno));
					_exit(1);
				}
			} else {
				fprintf(stderr, "%s", "error: unexpected output error\n");
				_exit(1);
			}
		}
	} while (bytes_written < len_txt);

	sqlite3 *conndb = NULL;
	char const * const namedb = "whatsapp-chat.db";
	rc = sqlite3_open(namedb, &conndb);
	if (SQLITE_OK != rc) {
		fprintf(stderr, "%s %s\n", "error: failed to open connection to database:", namedb);
		_exit(1);
	}

	char transdb[] = (
		"BEGIN TRANSACTION;\n"
		"CREATE TABLE IF NOT EXISTS users ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"name TEXT UNIQUE"
		");\n"
		"CREATE TABLE IF NOT EXISTS messages ("
		"msg_id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"usr_id INTEGER,"
		"timestamp DATETIME,"
		"content TEXT,"
		"FOREIGN KEY(usr_id) REFERENCES users(id)"
		");\n"
	);
	uint64_t const bytes_transdb = (sizeof(transdb) - 1);

	uint64_t const offset_sqlbase = (offset_mapbase + (timestamps * sizeof(*map)));

	offset = 0;
	memcpy(dstbuf + offset_sqlbase + offset, transdb, bytes_transdb);
	offset += bytes_transdb;

	offset_map = 0;
	for (uint32_t i = 0; i != timestamps; ++i, offset_map += sizeof(*map)) {

		map = (dstbuf + offset_mapbase + offset_map);
		if (map->size_user) {
			// NOTE: user names may have spaces so we need to use single quotes
			char insert[] = "INSERT OR IGNORE INTO users (name) VALUES ('";
			uint64_t const bytes_insert = (sizeof(insert) - 1);
			memcpy(dstbuf + offset_sqlbase + offset, insert, bytes_insert);
			offset += bytes_insert;

			memcpy(dstbuf + offset_sqlbase + offset, dstbuf + map->offset_user, map->size_user);
			offset += map->size_user;

			char trail_insert[] = "');\n";
			uint64_t const bytes_trailsert = (sizeof(trail_insert) - 1);
			memcpy(dstbuf + offset_sqlbase + offset, trail_insert, bytes_trailsert);
			offset += bytes_trailsert;

			char message_insert[] = (
				"INSERT INTO messages (usr_id, timestamp, content) "
				"VALUES ((SELECT id FROM users WHERE name = '"
			);
			uint64_t const bytes_mesert = (sizeof(message_insert) - 1);
			memcpy(dstbuf + offset_sqlbase + offset, message_insert, bytes_mesert);
			offset += bytes_mesert;

			memcpy(dstbuf + offset_sqlbase + offset, dstbuf + map->offset_user, map->size_user);
			offset += map->size_user;

			char content_insert[] = "'),'";
			uint64_t const bytes_consert = (sizeof(content_insert) - 1);
			memcpy(dstbuf + offset_sqlbase + offset, content_insert, bytes_consert);
			offset += bytes_consert;

			memset(mmddyy, 0, sizeof(mmddyy));
			uint64_t const bytes_timestamp = snprintf((void*) mmddyy, sizeof(mmddyy), "%ld", map->timestamp);
			if (bytes_timestamp >= sizeof(mmddyy)) {
				fprintf(stderr, "%s", "error: timestamp truncation\n");
				sqlite3_close(conndb);
				_exit(1);
			}

			memcpy(dstbuf + offset_sqlbase + offset, mmddyy, bytes_timestamp);
			offset += bytes_timestamp;

			char next_insert[] = "','";
			uint64_t const bytes_nexsert = (sizeof(next_insert) - 1);
			memcpy(dstbuf + offset_sqlbase + offset, next_insert, bytes_nexsert);
			offset += bytes_nexsert;

			memcpy(dstbuf + offset_sqlbase + offset, dstbuf + map->offset_chat, map->size_chat);
			offset += map->size_chat;

			memcpy(dstbuf + offset_sqlbase + offset, trail_insert, bytes_trailsert);
			offset += bytes_trailsert;

			if ((len_mmap - (offset_sqlbase + offset)) <= pagesz) {
				dstbuf = mremap(dstbuf, len_mmap, (len_mmap << 1), MREMAP_MAYMOVE);
				len_mmap <<= 1;
			}
		}
	}

	char const commit[] = "COMMIT;";
	uint64_t const bytes_commit = (sizeof(commit) - 1);
	memcpy(dstbuf + offset_sqlbase + offset, commit, bytes_commit);
	offset += bytes_commit;

	char *errmsg = NULL;
	rc = sqlite3_exec(conndb, dstbuf + offset_sqlbase, NULL, NULL, &errmsg);
	if (SQLITE_OK != rc) {
		fprintf(stderr, "%s", "error: SQL error\n");
		if (errmsg) {
			fprintf(stderr, "%s\n", errmsg);
		}
		sqlite3_close(conndb);
		_exit(1);
	}

	rc = sqlite3_close(conndb);
	if (SQLITE_OK != rc) {
		fprintf(stderr, "%s %s\n", "error: failed to close connection to database:", namedb);
		_exit(1);
	}
	return 0;

#if DEVBUILD
err:
	{
	    fprintf(stderr, "error on: %s:%d\n", __FILE__, lineno);
	    fprintf(stderr, "%s\n", strerror(errno));
	    _exit(1);
	}
err_uxchar_timestamp:
	{
	    fprintf(stderr, "error on: %s:%d\n", __FILE__, lineno);
	    fprintf(stderr, "%s\n", strerror(errno));
	    fprintf(stderr, "%s", "error: unexpected character encountered on conversion\n");
	    _exit(1);
	}
err_month_timestamp:
	{
	    fprintf(stderr, "error on: %s:%d\n", __FILE__, lineno);
	    fprintf(stderr, "%s", "error: invalid month value on conversion\n");
	    _exit(1);
	}
err_day_timestamp:
	{
	    fprintf(stderr, "error on: %s:%d\n", __FILE__, lineno);
	    fprintf(stderr, "%s", "error: invalid day value on conversion\n");
	    _exit(1);
	}
err_year_timestamp:
	{
	    fprintf(stderr, "error on: %s:%d\n", __FILE__, lineno);
	    fprintf(stderr, "%s", "error: invalid year value on conversion\n");
	    _exit(1);
	}
err_hour_timestamp:
	{
	    fprintf(stderr, "error on: %s:%d\n", __FILE__, lineno);
	    fprintf(stderr, "%s", "error: invalid hour value on conversion\n");
	    _exit(1);
	}
err_min_timestamp:
	{
	    fprintf(stderr, "error on: %s:%d\n", __FILE__, lineno);
	    fprintf(stderr, "%s", "error: invalid minute value on conversion\n");
	    _exit(1);
	}
err_encoding_timestamp:

//NOTE:
//mktime() sets the errno but that alone won't suffice to know if it failed, this is why we have to check the
//returned value and then maybe look at the errno for more info for completeness.

	{
	    fprintf(stderr, "error on: %s:%d\n", __FILE__, lineno);
	    fprintf(stderr, "%s", "error: invalid time value passed to mktime util\n");
	    if (errno) {
		    fprintf(stderr, "%s\n", strerror(errno));
	    }
	    _exit(1);
	}
err_uxlen_timestamp:
	{
	    fprintf(stderr, "error on: %s:%d\n", __FILE__, lineno);
	    fprintf(stderr, "%s", "error: unexpected time field (dd/mm/yy) length detected\n");
	    _exit(1);
	}
#endif
}
