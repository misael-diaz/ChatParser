#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define BOM_UTF8 0x00bfbbefu

// TODO: add offsets and size for the userid and message data
struct mapping {
	uint64_t offset_timestamp;
	uint64_t size_timestamp;
};

int main(int argc, char *argv[])
{
	if (argc < 1) {
		fprintf(stderr, "%s", "surprising command-line error (argc !> 0)\n");
		_exit(1);
	}

	int64_t rc = 0;
	struct stat st = {};
	rc = fstat(STDIN_FILENO, &st);
	if (!S_ISFIFO(st.st_mode)) {
		fprintf(stderr,
			"%s %s",
			argv[0],
			"expects input to come from a pipe\n");
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
			_exit(1);
		}
	} else {
		fprintf(stderr,
			"%s %s",
			argv[0],
			"expects input to come from a stream-like pipe (a not seekable pipe)\n");
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
	fprintf(stdout, "chat-length (bytes): %lu\n", len_chat);
	fprintf(stdout, "mmap-size (bytes): %lu\n", len_mmap);

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
			}
			else if ((value >= 0x88c3u) && (value < 0x8cc3u)) {
				*dst = 'e';
			}
			else if ((value >= 0x8cc3u) && (value < 0x90c3u)) {
				*dst = 'i';
			}
			else if ((value >= 0x92c3u) && (value < 0x97c3u)) {
				*dst = 'o';
			}
			else if ((value >= 0x99c3u) && (value < 0x9ec3u)) {
				fprintf(stdout, "%c", 'u');
				*dst = 'u';
			}
			else if ((value >= 0xa0c3u) && (value < 0xa6c3u)) {
				*dst = 'a';
			}
			else if ((value >= 0xa8c3u) && (value < 0xacc3u)) {
				*dst = 'e';
			}
			else if ((value >= 0xacc3u) && (value < 0xb0c3u)) {
				*dst = 'i';
			}
			else if ((value == 0xb1c3u)) {
				*dst = 'n';
			}
			else if ((value >= 0xb2c3u) && (value < 0xb7c3u)) {
				*dst = 'o';
			}
			else if ((value >= 0xb9c3u) && (value < 0xbdc3u)) {
				*dst = 'u';
			}
			txt += 2;
			dst += 1;
			len_txt += 1;
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
	else {
		fprintf(stdout, "%s %lu %s", "bytes-read:", count, "\n");
		fprintf(stdout, "%s %lu %s", "bytes-kept:", len_txt, "\n");
	}

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
			_exit(1);
		}
		else if ((*dst >= 0x0bu) && (*dst < 0x20u)) {
			fprintf(stderr, "%s", normerr);
			_exit(1);
		}
		else if (((*dst) >= 0x41u) && ((*dst) < 0x5bu)) {
			fprintf(stderr, "%s", folderr);
			_exit(1);
		}
		else if (*dst >= 0x7fu) {
			fprintf(stderr, "%s", tliterr);
			_exit(1);
		}
	}
	fprintf(stdout, "%s", (char*) dstbuf);

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
	int64_t const isdst = 0;
	uint64_t timestamps = 0;
	uint32_t lineno = 0;
	uint8_t sz_timestamp = 0;
	void *vptr = NULL;
	char *endptr = NULL;
	char *nl = NULL;
	char *dm = NULL;
	struct tm timestamp = {};
	struct tm * const tp = &timestamp;
	struct mapping *map = (dstbuf + ((len_txt + 0x1fu) & ~0x1fu));
	char unsigned mmddyy[32];
	memset(mmddyy, 0, sizeof(mmddyy));
	for (int i = 0; i != len_txt; ++i, ++dst) {
	    if ((dst[0] >= 0x30u) && (dst[0] < 0x3au)) {
		if ('/' == dst[1]) {

		    if ((dst[2] >= 0x30u) && (dst[2] < 0x3au)) {
			if ('/' == dst[3]) {

			    if (
				    (dst[4] >= 0x30u) && (dst[4] < 0x3au) &&
				    (dst[5] >= 0x30u) && (dst[5] < 0x3au) &&
				    (dst[6] == ',') &&
				    (dst[7] == ' ') && (
					(
					 (dst[8] >= 0x30u) && (dst[8] < 0x3au) &&
					 (dst[9] == ':') &&
					 (dst[10] >= 0x30u) && (dst[10] < 0x3au) &&
					 (dst[11] >= 0x30u) && (dst[11] < 0x3au)
					) ||
					(
					 (dst[8] >= 0x30u) && (dst[8] < 0x3au) &&
					 (dst[9] >= 0x30u) && (dst[9] < 0x3au) &&
					 (dst[10] == ':') &&
					 (dst[11] >= 0x30u) && (dst[11] < 0x3au) &&
					 (dst[12] >= 0x30u) && (dst[12] < 0x3au)
					)
				    )
			       ) {

				errno = 0;
				vptr = &dst[0];
				endptr = NULL;
				lineno = (1 + (__LINE__));
				mon = (strtol(vptr, &endptr, 0) - 1);
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
				vptr = &dst[2];
				endptr = NULL;
				lineno = (1 + (__LINE__));
				mday = strtol(vptr, &endptr, 0);
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
				vptr = &dst[4];
				endptr = NULL;
				lineno = (1 + (__LINE__));
				year = (strtol(vptr, &endptr, 0) + (2000 - 1900));
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
				vptr = &dst[8];
				endptr = NULL;
				lineno = (1 + (__LINE__));
				hour = strtol(vptr, &endptr, 0);
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
				tmin = strtol(vptr, &endptr, 0);
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
				    hour += 12;
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

				vptr = dst;
				nl = strstr(vptr, "\n");
				dm = strstr(vptr, "-");
				if (nl && dm) {
					if (dm < nl) {
						map->offset_timestamp = (vptr - dstbuf);
						map->size_timestamp = (((void*) dm) - vptr);
						++timestamps;
						++map;
					}
				}

				uint16_t const AntePostMeridiemValue = ((dst[13] << 8) | dst[12]);
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
				fprintf(stdout, "timestamp: %s mm/dd/yy, hh:mm %ld/%ld/%ld, %ld:%ld encoding: %ld\n", mmddyy, mon, mday, year, hour, tmin, encoded_time);
				memset(mmddyy, 0, sizeof(mmddyy));
			    }
			}
			else if ((dst[3] >= 0x30u) && (dst[3] < 0x3au)) {
			    if ('/' == dst[4]) {

				if (
					(dst[5] >= 0x30u) && (dst[5] < 0x3au) &&
					(dst[6] >= 0x30u) && (dst[6] < 0x3au) &&
					(dst[7] == ',') &&
					(dst[8] == ' ') && (
					    (
					     (dst[9] >= 0x30u) && (dst[9] < 0x3au) &&
					     (dst[10] == ':') &&
					     (dst[11] >= 0x30u) && (dst[11] < 0x3au) &&
					     (dst[12] >= 0x30u) && (dst[12] < 0x3au)
					    ) ||
					    (
					     (dst[9] >= 0x30u) && (dst[9] < 0x3au) &&
					     (dst[10] >= 0x30u) && (dst[10] < 0x3au) &&
					     (dst[11] == ':') &&
					     (dst[12] >= 0x30u) && (dst[12] < 0x3au) &&
					     (dst[13] >= 0x30u) && (dst[13] < 0x3au)
					    )
					)
				   ) {

				    errno = 0;
				    vptr = &dst[0];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    mon = (strtol(vptr, &endptr, 0) - 1);
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
				    vptr = &dst[3];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    mday = strtol(vptr, &endptr, 0);
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
				    vptr = &dst[5];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    year = (strtol(vptr, &endptr, 0) + (2000 - 1900));
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
				    vptr = &dst[9];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    hour = strtol(vptr, &endptr, 0);
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
				    tmin = strtol(vptr, &endptr, 0);
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
					hour += 12;
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

				    vptr = dst;
				    nl = strstr(vptr, "\n");
				    dm = strstr(vptr, "-");
				    if (nl && dm) {
					    if (dm < nl) {
						    map->offset_timestamp = (vptr - dstbuf);
						    map->size_timestamp = (((void*) dm) - vptr);
						    ++timestamps;
						    ++map;
					    }
				    }

				    uint16_t const AntePostMeridiemValue = ((dst[14] << 8) | dst[13]);
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
				    fprintf(stdout, "timestamp: %s mm/dd/yy, hh:mm %ld/%ld/%ld, %ld:%ld encoding: %ld\n", mmddyy, mon, mday, year, hour, tmin, encoded_time);
				    memset(mmddyy, 0, sizeof(mmddyy));
				}
			    }
			}
		    }
		}
		else if ((dst[1] >= 0x30u) && (dst[1] < 0x3au)) {
		    if ('/' == dst[2]) {

			if ((dst[3] >= 0x30u) && (dst[3] < 0x3au)) {
			    if ('/' == dst[4]) {

				if (
					(dst[5] >= 0x30u) && (dst[5] < 0x3au) &&
					(dst[6] >= 0x30u) && (dst[6] < 0x3au) &&
					(dst[7] == ',') &&
					(dst[8] == ' ') && (
					    (
					     (dst[9] >= 0x30u) && (dst[9] < 0x3au) &&
					     (dst[10] == ':') &&
					     (dst[11] >= 0x30u) && (dst[11] < 0x3au) &&
					     (dst[12] >= 0x30u) && (dst[12] < 0x3au)
					    ) ||
					    (
					     (dst[9] >= 0x30u) && (dst[9] < 0x3au) &&
					     (dst[10] >= 0x30u) && (dst[10] < 0x3au) &&
					     (dst[11] == ':') &&
					     (dst[12] >= 0x30u) && (dst[12] < 0x3au) &&
					     (dst[13] >= 0x30u) && (dst[13] < 0x3au)
					    )
					)
				   ) {

				    errno = 0;
				    vptr = &dst[0];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    mon = (strtol(vptr, &endptr, 0) - 1);
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
				    vptr = &dst[3];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    mday = strtol(vptr, &endptr, 0);
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
				    vptr = &dst[5];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    year = (strtol(vptr, &endptr, 0) + (2000 - 1900));
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
				    vptr = &dst[9];
				    endptr = NULL;
				    lineno = (1 + (__LINE__));
				    hour = strtol(vptr, &endptr, 0);
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
				    tmin = strtol(vptr, &endptr, 0);
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
					hour += 12;
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

				    vptr = dst;
				    nl = strstr(vptr, "\n");
				    dm = strstr(vptr, "-");
				    if (nl && dm) {
					    if (dm < nl) {
						    map->offset_timestamp = (vptr - dstbuf);
						    map->size_timestamp = (((void*) dm) - vptr);
						    ++timestamps;
						    ++map;
					    }
				    }

				    uint16_t const AntePostMeridiemValue = ((dst[14] << 8) | dst[13]);
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
				    fprintf(stdout, "timestamp: %s mm/dd/yy, hh:mm %ld/%ld/%ld, %ld:%ld encoding: %ld\n", mmddyy, mon, mday, year, hour, tmin, encoded_time);
				    memset(mmddyy, 0, sizeof(mmddyy));
				}
			    }
			    else if ((dst[4] >= 0x30u) && (dst[4] < 0x3au)) {
				if ('/' == dst[5]) {

				    if (
					    (dst[6] >= 0x30u) && (dst[6] < 0x3au) &&
					    (dst[7] >= 0x30u) && (dst[7] < 0x3au) &&
					    (dst[8] == ',') &&
					    (dst[9] == ' ') && (
						(
						 (dst[10] >= 0x30u) && (dst[10] < 0x3au) &&
						 (dst[11] == ':') &&
						 (dst[12] >= 0x30u) && (dst[12] < 0x3au) &&
						 (dst[13] >= 0x30u) && (dst[13] < 0x3au)
						) ||
						(
						 (dst[10] >= 0x30u) && (dst[10] < 0x3au) &&
						 (dst[11] >= 0x30u) && (dst[11] < 0x3au) &&
						 (dst[12] == ':') &&
						 (dst[13] >= 0x30u) && (dst[13] < 0x3au) &&
						 (dst[14] >= 0x30u) && (dst[14] < 0x3au)
						)
					    )
				       ) {

					errno = 0;
					vptr = &dst[0];
					endptr = NULL;
					lineno = (1 + (__LINE__));
					mon = (strtol(vptr, &endptr, 0) - 1);
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
					vptr = &dst[3];
					endptr = NULL;
					lineno = (1 + (__LINE__));
					mday = strtol(vptr, &endptr, 0);
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
					vptr = &dst[6];
					endptr = NULL;
					lineno = (1 + (__LINE__));
					year = (strtol(vptr, &endptr, 0) + (2000 - 1900));
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
					vptr = &dst[10];
					endptr = NULL;
					lineno = (1 + (__LINE__));
					hour = strtol(vptr, &endptr, 0);
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
					tmin = strtol(vptr, &endptr, 0);
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
					    hour += 12;
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

					vptr = dst;
					nl = strstr(vptr, "\n");
					dm = strstr(vptr, "-");
					if (nl && dm) {
						if (dm < nl) {
							map->offset_timestamp = (vptr - dstbuf);
							map->size_timestamp = (((void*) dm) - vptr);
							++timestamps;
							++map;
						}
					}

					uint16_t const AntePostMeridiemValue = ((dst[15] << 8) | dst[14]);
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
					fprintf(stdout, "timestamp: %s mm/dd/yy, hh:mm %ld/%ld/%ld, %ld:%ld encoding: %ld\n", mmddyy, mon, mday, year, hour, tmin, encoded_time);
					memset(mmddyy, 0, sizeof(mmddyy));
				    }
				}
			    }
			}
		    }
		}
	    }
	}

	// checks the chat mapping array (timestamp, userid, and message)
	fprintf(stdout, "timestamps: %lu\n", timestamps);
	map = (dstbuf + ((len_txt + 0x1fu) & ~0x1fu));
	for (uint32_t i = 0; i != timestamps; ++i, ++map) {
		if (sizeof(mmddyy) > map->size_timestamp) {
			memset(mmddyy, 0, sizeof(mmddyy));
			memcpy(mmddyy, dstbuf + map->offset_timestamp, map->size_timestamp);
			fprintf(stdout, "%s\n", mmddyy);
		}
		else {
			fprintf(stdout, "%s", "would overrun timestamp placeholder\n");
		}
	}

//EXPERIMENTAL TIMESTAMPS CODE:
//
//The following experimental code is going to be removed in a future commit since this is just for verification.
//
//The experimental code gives us the idea of what the chat-parser should do with timestamps:
//construct a `struct tm` -> mktime() -> time_t (64-bit integer) representating the number of seconds since
//the Unix Epoch.
//
//The advantage of doing this is that the math is simple, sorting is also simple, and the
//time data is unambiguous regardless of the location where the database is hosted.
//
//Key points here, setting isdst to zero, meaning no daylight savings, and this makes sense for my use case.
//The other point is to set the timezone before calling `mktime` so that system timezone won't interfere
//with the timestamps. This matters when the system timezone does not match the timezone of the chats,
//and this is my case.

	struct tm t = {};
	t.tm_sec = 18;
	t.tm_min = 18;
	t.tm_hour = 18;
	t.tm_mday = 4;
	t.tm_mon = 4;
	t.tm_year = 2026 - 1900;
	t.tm_isdst = 0;
	setenv("TZ", "EST-5:00:00", 1);
	int64_t time = mktime(&t);
	fprintf(stdout, "%s\n", *tzname);
	fprintf(stdout, "%s\n", getenv("TZ"));
	fprintf(stdout, "secs: %ld\n", time);
#endif
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
